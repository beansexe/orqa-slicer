#include "VendorAccountsDialog.hpp"
#include "FlashforgeAccount.hpp"
#include "FlashforgeSessionSettings.hpp"
#include "printers/PrinterPickerDialog.hpp"
#include "devices/FlashforgeDevicePanel.hpp"
#include "printing/FlashforgeCloudPrint.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/AppConfig.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/Utils/CloudProvider.hpp"
#include "slic3r/Utils/NetworkAgent.hpp"
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/defs.h>
#include <wx/debug.h>
#include <wx/dialog.h>
#include <wx/event.h>
#include <wx/filedlg.h>
#include <wx/filefn.h>
#include <wx/gdicmn.h>
#include <wx/listbox.h>
#include <wx/log.h>
#include <wx/msgdlg.h>
#include <wx/secretstore.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/statbox.h>
#include <wx/string.h>
#include <wx/textctrl.h>
#include <wx/timer.h>
#include <wx/toplevel.h>
#include <wx/thread.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <exception>
#include <functional>
#include <future>
#include <initializer_list>
#include <memory>
#include <string>
#include <utility>

namespace Slic3r { namespace GUI {
namespace {
// Preserved across dialog openings, independent of GUI_App's Bambu agent.
std::shared_ptr<FlashforgeAccount> flashforge_account;
// Preserve the identifier when this dialog is reopened during the same session.
// The password is never retained here.
std::string flashforge_username;
// One worker serves both the modal account UI and explicit Device refreshes.
// Its destructor joins before the account owner (declared above) is destroyed.
std::future<bool> flashforge_work;
bool flashforge_dialog_open = false;
bool flashforge_external_work = false;
enum class ExternalOperation { list, status, print };
ExternalOperation flashforge_operation = ExternalOperation::list;
std::shared_ptr<std::atomic<bool>> flashforge_cancel;
std::shared_ptr<VendorPrinting::CloudPrintResult> flashforge_print_result;
bool flashforge_closing = false;
FlashforgeAccountSnapshot flashforge_snapshot;
std::function<void()> flashforge_dialog_drain;

wxString flashforge_secret_service()
{
    return "OrqaSlicer.Flashforge." + wxString::FromUTF8(data_dir());
}

void invalidate_flashforge_snapshot()
{
    if (++flashforge_snapshot.generation == 0) ++flashforge_snapshot.generation;
}

void update_flashforge_snapshot(bool list_operation, bool succeeded)
{
    if (flashforge_account) {
        flashforge_snapshot.state = flashforge_account->state();
        flashforge_snapshot.devices = flashforge_account->devices();
        flashforge_snapshot.message = flashforge_account->message();
        flashforge_snapshot.live = flashforge_account->live_status();
    }
    if (list_operation) {
        flashforge_snapshot.refresh_failed = !succeeded;
        if (succeeded && flashforge_snapshot.state == FlashforgeAccount::State::signed_in)
            flashforge_snapshot.observed_at = std::chrono::steady_clock::now();
    }
    if (flashforge_snapshot.state != FlashforgeAccount::State::signed_in) {
        flashforge_snapshot.observed_at = {};
        flashforge_snapshot.live.reset();
        flashforge_snapshot.live_observed_at = {};
        flashforge_snapshot.cloud_job_message.clear();
        flashforge_snapshot.cloud_job_id.clear();
        if (!list_operation) flashforge_snapshot.refresh_failed = false;
    }
}

// Explicit Device refreshes can rotate the token while the account dialog is
// closed. Persist that rotation on the UI thread, retaining the existing policy.
void save_background_secret()
{
    auto* config = wxGetApp().app_config;
    if (!config || !flashforge_account || config->get("flashforge_account_remember") == "0" ||
        config->get(flashforge_restoration_setting) == "1") return;
    if (flashforge_account->state() == FlashforgeAccount::State::signed_out && flashforge_account->session_secret().empty()) {
        if (!persist_flashforge_restoration_disabled(*config, true)) {
            flashforge_snapshot.message = "Flashforge account settings could not be saved. Check that the data folder is writable.";
            return;
        }
        auto store = wxSecretStore::GetDefault();
        if (store.IsOk()) {
            wxLogCollector errors;
            store.Delete(flashforge_secret_service());
        }
        return;
    }
    if (flashforge_account->state() != FlashforgeAccount::State::signed_in) return;
    auto store = wxSecretStore::GetDefault();
    wxSecretValue secret(wxString::FromUTF8(flashforge_account->session_secret()));
    wxLogCollector errors;
    if (!store.IsOk() || !store.Save(flashforge_secret_service(), wxString::FromUTF8(flashforge_username), secret))
        flashforge_snapshot.message = "Signed in for this session. The system credential store could not save your sign-in.";
}

void poll_background_refresh()
{
    if (!flashforge_external_work || flashforge_dialog_open || !flashforge_work.valid() ||
        flashforge_work.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    bool succeeded = false;
    try { succeeded = flashforge_work.get(); } catch (const std::exception&) {}
    flashforge_external_work = false;
    update_flashforge_snapshot(flashforge_operation == ExternalOperation::list, succeeded);
    if (flashforge_operation == ExternalOperation::status && succeeded)
        flashforge_snapshot.live_observed_at = std::chrono::steady_clock::now();
    if (flashforge_operation == ExternalOperation::print && flashforge_print_result) {
        flashforge_snapshot.cloud_job_message = flashforge_print_result->message;
        flashforge_snapshot.cloud_job_id = flashforge_print_result->job_id;
    }
    flashforge_snapshot.cloud_job_running = false;
    save_background_secret();
}

class VendorAccountsDialog final : public wxDialog {
public:
    explicit VendorAccountsDialog(wxWindow* parent)
        : wxDialog(parent, wxID_ANY, _L("Printer accounts"), wxDefaultPosition,
                   wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER), m_timer(this)
    {
        m_service = flashforge_secret_service();
        auto* layout = new wxBoxSizer(wxVERTICAL);
        layout->Add(new wxStaticText(this, wxID_ANY,
            _L("Bambu and Flashforge accounts are independent. You can remain signed in to both.")),
            0, wxALL, FromDIP(12));
        auto* bambu = new wxStaticBoxSizer(wxVERTICAL, this, _L("Bambu Lab"));
        m_bambu_status = new wxStaticText(this, wxID_ANY, "");
        bambu->Add(m_bambu_status, 0, wxALL, FromDIP(8));
        auto* bambu_buttons = new wxBoxSizer(wxHORIZONTAL);
        auto* bambu_login = new wxButton(this, wxID_ANY, _L("Bambu account"));
        auto* bambu_logout = new wxButton(this, wxID_ANY, _L("Sign out of Bambu"));
        bambu_buttons->Add(bambu_login, 0, wxRIGHT, FromDIP(8));
        bambu_buttons->Add(bambu_logout);
        bambu->Add(bambu_buttons, 0, wxALL, FromDIP(8));
        layout->Add(bambu, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(12));
        bambu_login->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            auto* agent = wxGetApp().getAgent();
            if (!agent || !agent->get_cloud_agent(BBL_CLOUD_PROVIDER)) {
                wxMessageBox(_L("Bambu account service is unavailable. Enable Orca's Bambu networking integration, then reopen Printer accounts."),
                             _L("Bambu account"), wxOK | wxICON_INFORMATION, this);
                return;
            }
            EndModal(wxID_OK);
            wxGetApp().CallAfter([] {
                auto* agent = wxGetApp().getAgent();
                if (agent && agent->get_cloud_agent(BBL_CLOUD_PROVIDER))
                    wxGetApp().request_login(true, BBL_CLOUD_PROVIDER);
            });
        });
        bambu_logout->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            EndModal(wxID_OK);
            wxGetApp().CallAfter([] { wxGetApp().request_user_logout(BBL_CLOUD_PROVIDER); });
        });
        m_bambu_buttons = bambu_buttons;

        auto* flash = new wxStaticBoxSizer(wxVERTICAL, this, _L("Flashforge"));
        m_status = new wxStaticText(this, wxID_ANY, _L("Checking Flashforge networking plugin…"));
        m_status->Wrap(FromDIP(540));
        flash->Add(m_status, 0, wxALL | wxEXPAND, FromDIP(8));
        flash->Add(new wxStaticText(this, wxID_ANY, _L("Email or phone number")), 0, wxLEFT, FromDIP(8));
        m_username = new wxTextCtrl(this, wxID_ANY);
        m_username->SetValue(wxString::FromUTF8(flashforge_username));
        flash->Add(m_username, 0, wxEXPAND | wxALL, FromDIP(8));
        flash->Add(new wxStaticText(this, wxID_ANY, _L("Password")), 0, wxLEFT, FromDIP(8));
        m_password = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_PASSWORD);
        flash->Add(m_password, 0, wxEXPAND | wxALL, FromDIP(8));
        m_remember = new wxCheckBox(this, wxID_ANY, _L("Keep me signed in using the system credential store"));
        m_remember->SetValue(wxGetApp().app_config->get("flashforge_account_remember") != "0");
        flash->Add(m_remember, 0, wxALL, FromDIP(8));
        auto* buttons = new wxBoxSizer(wxHORIZONTAL);
        m_login = new wxButton(this, wxID_ANY, _L("Sign in to Flashforge"));
        m_logout = new wxButton(this, wxID_ANY, _L("Sign out of Flashforge"));
        m_refresh = new wxButton(this, wxID_ANY, _L("Refresh printers"));
        m_open_device = new wxButton(this, wxID_ANY, _L("Open device"));
        m_plugin = new wxButton(this, wxID_ANY, _L("Choose plugin"));
        for (auto* button : {m_login, m_logout, m_refresh, m_open_device, m_plugin})
            buttons->Add(button, 0, wxRIGHT, FromDIP(6));
        flash->Add(buttons, 0, wxALL, FromDIP(8));
        m_devices = new wxListBox(this, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(540), FromDIP(110)));
        flash->Add(m_devices, 1, wxEXPAND | wxALL, FromDIP(8));
        flash->Add(new wxStaticText(this, wxID_ANY,
            _L("Account preview: Flashforge printers listed here are not yet connected to wireless job submission.")),
            0, wxALL, FromDIP(8));
        layout->Add(flash, 1, wxEXPAND | wxALL, FromDIP(12));
        m_close = new wxButton(this, wxID_CANCEL, _L("Close"));
        layout->Add(m_close, 0, wxALIGN_RIGHT | wxALL, FromDIP(12));
        SetSizerAndFit(layout);
        wxGetApp().UpdateDlgDarkUI(this);
        CentreOnParent();
        Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& event) {
            if (busy() && event.CanVeto()) { event.Veto(); return; }
            event.Skip();
        });
        Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            if (!busy()) EndModal(wxID_CANCEL);
        }, wxID_CANCEL);
        m_login->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            std::string username = m_username->GetValue().ToUTF8().data();
            std::string password = m_password->GetValue().ToUTF8().data();
            m_password->Clear();
            // A replacement sign-in must not leave an older saved account
            // eligible for restoration if saving the new token later fails.
            if (!username.empty() && !password.empty() && !persist_restoration_disabled(true)) return;
            flashforge_username = username;
            start([username = std::move(username), password = std::move(password)] {
                return flashforge_account->login(username, password);
            }, false, true);
        });
        m_logout->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            if (!erase_secret()) return;
            start([] { flashforge_account->logout(); return true; });
        });
        m_refresh->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            start([] { return flashforge_account->refresh_devices(); });
        });
        m_open_device->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { open_device(); });
        m_devices->Bind(wxEVT_LISTBOX, [this](wxCommandEvent&) { update_open_device(); });
        m_devices->Bind(wxEVT_LISTBOX_DCLICK, [this](wxCommandEvent&) { open_device(); });
        m_plugin->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { choose_plugin(); });
        m_remember->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) {
            auto* config = wxGetApp().app_config;
            const std::string previous = config->get("flashforge_account_remember");
            config->set("flashforge_account_remember", m_remember->GetValue() ? "1" : "0");
            if (!m_remember->GetValue() && !erase_secret()) {
                config->set("flashforge_account_remember", previous);
                m_remember->SetValue(true);
            } else if (m_remember->GetValue()) {
                save_secret();
            }
        });
        Bind(wxEVT_TIMER, [this](wxTimerEvent&) { poll(); });
        m_timer.Start(150);
        if (flashforge_account) {
            update();
            if (flashforge_account->state() != FlashforgeAccount::State::unavailable)
                restore_or_refresh();
        } else {
            auto library = wxGetApp().app_config->get("flashforge_account_plugin");
            auto settings = wxGetApp().app_config->get("flashforge_account_settings");
#ifdef _WIN32
            // Read the existing vendor installation; never copy or redistribute its DLL.
            const wxString installed = "C:\\Program Files\\Flashforge\\Orca-Flashforge\\";
            if (library.empty() && wxFileExists(installed + "FlashNetwork.dll")) {
                library = (installed + "FlashNetwork.dll").ToUTF8().data();
                settings = (installed + "resources\\data\\FLASHNETWORK9.DAT").ToUTF8().data();
            }
#endif
            start([library, settings] {
                flashforge_account = FlashforgeAccount::load(library, settings);
                return flashforge_account->state() != FlashforgeAccount::State::unavailable;
            }, true);
        }
    }
    ~VendorAccountsDialog() override
    {
        flashforge_dialog_drain = {};
        m_timer.Stop();
        // A non-vetoable app shutdown may destroy the dialog mid-call. Join the
        // worker before plugin/session destruction; worker never references UI.
        if (flashforge_work.valid()) {
            bool succeeded = false;
            try { succeeded = flashforge_work.get(); } catch (const std::exception&) {}
            update_flashforge_snapshot(!m_restore_after_load, succeeded);
        }
    }
    const std::string& selected_device() const { return m_selected_device; }
    void register_shutdown_drain()
    {
        flashforge_dialog_drain = [this] { drain_for_shutdown(); };
    }
private:
    wxTimer m_timer;
    bool m_restore_after_load = false;
    bool m_new_login = false;
    wxString m_service;
    wxStaticText *m_bambu_status = nullptr, *m_status = nullptr;
    wxTextCtrl *m_username = nullptr, *m_password = nullptr;
    wxCheckBox* m_remember = nullptr;
    wxListBox* m_devices = nullptr;
    wxButton *m_login = nullptr, *m_logout = nullptr, *m_refresh = nullptr,
             *m_open_device = nullptr, *m_plugin = nullptr, *m_close = nullptr;
    wxBoxSizer* m_bambu_buttons = nullptr;
    std::string m_selected_device;
    bool busy() const { return flashforge_work.valid(); }

    void drain_for_shutdown()
    {
        m_timer.Stop();
        if (!busy()) return;
        bool succeeded = false;
        try { succeeded = flashforge_work.get(); } catch (const std::exception&) {}
        const bool was_load = std::exchange(m_restore_after_load, false);
        const bool new_login = std::exchange(m_new_login, false);
        update_flashforge_snapshot(!was_load, succeeded);
        // A plugin load does not start a restore while shutting down. A completed
        // login/refresh must save its rotated token before AppConfig is deleted.
        if (!was_load) save_secret(new_login, true);
    }

    void set_account_status(const wxString& message, bool quiet)
    {
        flashforge_snapshot.message = message.ToUTF8().data();
        if (!quiet) m_status->SetLabel(message);
    }

    void start(std::function<bool()> operation, bool restore_after_load = false, bool new_login = false)
    {
        if (busy() || wxGetApp().is_closing()) return;
        m_restore_after_load = restore_after_load;
        m_new_login = new_login;
        m_status->SetLabel(_L("Connecting…"));
        for (auto* control : {m_login, m_logout, m_refresh, m_open_device, m_plugin, m_close}) control->Disable();
        m_username->Disable();
        m_password->Disable();
        m_remember->Disable();
        // Disallow leaving through Bambu's buttons while the worker is active.
        for (const auto* item : m_bambu_buttons->GetChildren()) item->GetWindow()->Disable();
        invalidate_flashforge_snapshot();
        try {
            flashforge_work = std::async(std::launch::async, std::move(operation));
        } catch (const std::exception&) {
            m_restore_after_load = false;
            m_new_login = false;
            update();
            m_status->SetLabel(_L("The account worker could not start. Please try again."));
        }
    }
    void poll()
    {
        const bool bambu_signed_in = wxGetApp().is_user_login(BBL_CLOUD_PROVIDER);
        auto* agent = wxGetApp().getAgent();
        const bool bambu_connected = bambu_signed_in && agent &&
            agent->is_server_connected(BBL_CLOUD_PROVIDER);
        m_bambu_status->SetLabel(bambu_signed_in
            ? (bambu_connected ? _L("Signed in to Bambu Lab — cloud connected")
                               : _L("Signed in to Bambu Lab — cloud disconnected"))
            : _L("Signed out of Bambu Lab"));
        if (!busy()) return;
        if (flashforge_work.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
        bool succeeded = true;
        bool refreshed = false;
        try { refreshed = flashforge_work.get(); } catch (const std::exception&) { succeeded = false; }
        update_flashforge_snapshot(!m_restore_after_load, refreshed);
        const bool new_login = std::exchange(m_new_login, false);
        update();
        if (!succeeded) {
            m_status->SetLabel(_L("Flashforge account operation failed. Please try again."));
            return;
        }
        if (m_restore_after_load) {
            m_restore_after_load = false;
            if (flashforge_account && flashforge_account->state() != FlashforgeAccount::State::unavailable)
                restore_or_refresh();
        } else {
            save_secret(new_login);
        }
    }
    void update()
    {
        if (busy()) return;
        const bool available = flashforge_account && flashforge_account->state() != FlashforgeAccount::State::unavailable;
        const bool signed_in = available && flashforge_account->state() == FlashforgeAccount::State::signed_in;
        m_login->Enable(available && !signed_in);
        m_logout->Enable(available && (signed_in || !flashforge_account->session_secret().empty()));
        m_refresh->Enable(signed_in);
        m_username->Enable(available && !signed_in);
        m_password->Enable(available && !signed_in);
        m_plugin->Enable(!signed_in);
        m_close->Enable();
        for (const auto* item : m_bambu_buttons->GetChildren()) item->GetWindow()->Enable();
        auto store = wxSecretStore::GetDefault();
        m_remember->Enable(store.IsOk());
        if (!store.IsOk()) m_remember->SetValue(false);
        m_devices->Clear();
        if (flashforge_account) {
            m_status->SetLabel(wxString::FromUTF8(flashforge_account->message()));
            m_status->Wrap(FromDIP(540));
            for (const auto& device : flashforge_account->devices())
                m_devices->Append(wxString::FromUTF8(device.name + " | " + device.model + " | " + device.status));
        }
        update_open_device();
        Layout();
    }
    void update_open_device()
    {
        m_open_device->Enable(!busy() && flashforge_account &&
            flashforge_account->state() == FlashforgeAccount::State::signed_in && m_devices->GetSelection() != wxNOT_FOUND);
    }
    void open_device()
    {
        if (busy() || !flashforge_account || flashforge_account->state() != FlashforgeAccount::State::signed_in) return;
        const int selection = m_devices->GetSelection();
        const auto& devices = flashforge_account->devices();
        if (selection == wxNOT_FOUND || static_cast<std::size_t>(selection) >= devices.size()) return;
        m_selected_device = devices[static_cast<std::size_t>(selection)].id;
        EndModal(wxID_OK);
    }
    void restore_or_refresh()
    {
        if (flashforge_account->state() == FlashforgeAccount::State::signed_in) {
            start([] { return flashforge_account->refresh_devices(); });
            return;
        }
        if (!m_remember->GetValue() || wxGetApp().app_config->get(flashforge_restoration_setting) == "1") return;
        auto store = wxSecretStore::GetDefault();
        wxString username;
        wxSecretValue secret;
        if (store.IsOk() && store.Load(m_service, username, secret) && secret.IsOk()) {
            m_username->SetValue(username);
            flashforge_username = username.ToUTF8().data();
            std::string refresh(static_cast<const char*>(secret.GetData()), secret.GetSize());
            start([refresh = std::move(refresh)] { return flashforge_account->restore(refresh); });
        }
    }
    bool persist_restoration_disabled(bool disabled, bool quiet = false)
    {
        if (persist_flashforge_restoration_disabled(*wxGetApp().app_config, disabled)) return true;
        if (quiet) {
            set_account_status(_L("Flashforge account settings could not be saved. Check that the data folder is writable."), true);
            return false;
        }
        wxMessageBox(_L("Flashforge account settings could not be saved. The account change has not been completed. Check that the slicer's data folder is writable, then try again."),
                     _L("Flashforge account"), wxOK | wxICON_ERROR, this);
        return false;
    }
    bool erase_secret(bool quiet = false)
    {
        // The durable flag is authoritative even if a locked credential store
        // cannot remove its token. Do not mistake a failed Load for absence.
        if (!persist_restoration_disabled(true, quiet)) return false;
        auto store = wxSecretStore::GetDefault();
        bool deletion_failed = !store.IsOk();
        if (!deletion_failed) {
            wxLogCollector errors;
            const bool deleted = store.Delete(m_service);
            // wxWidgets reports a missing credential silently, but logs an
            // actual deletion error on Windows, macOS and Linux.
            deletion_failed = !deleted && !errors.GetMessages().empty();
        }
        if (deletion_failed && !quiet)
            wxMessageBox(_L("Automatic Flashforge sign-in has been disabled. The system credential store could not remove the saved sign-in; local sign-out can still continue."),
                         _L("Flashforge account"), wxOK | wxICON_INFORMATION, this);
        return true; // Restoration is disabled on disk; local logout is safe.
    }
    void save_secret(bool new_login = false, bool quiet = false)
    {
        if (!flashforge_account || !m_remember->GetValue()) return;
        const bool disabled = wxGetApp().app_config->get(flashforge_restoration_setting) == "1";
        if (flashforge_account->state() == FlashforgeAccount::State::signed_out &&
            flashforge_account->session_secret().empty()) {
            if (!disabled) erase_secret(quiet); // Do not restore an explicitly invalidated token on restart.
            return;
        }
        if (flashforge_account->state() != FlashforgeAccount::State::signed_in) return;
        if (disabled && !new_login) {
            set_account_status(_L("Signed in for this session. Sign out and sign in again to keep this account remembered."), quiet);
            return;
        }
        auto store = wxSecretStore::GetDefault();
        wxSecretValue secret(wxString::FromUTF8(flashforge_account->session_secret()));
        wxLogCollector errors;
        if (!store.IsOk() || !store.Save(m_service, m_username->GetValue(), secret)) {
            set_account_status(_L("Signed in for this session. The system credential store could not save your sign-in."), quiet);
            return;
        }
        // A checkbox toggle or background refresh cannot revive an account
        // explicitly signed out earlier; only a saved new login may do so.
        if (new_login && !persist_restoration_disabled(false, quiet))
            set_account_status(_L("Signed in for this session. Automatic sign-in remains disabled because account settings could not be saved."), quiet);
    }
    void choose_plugin()
    {
        wxFileDialog plugin(this, _L("Choose the official Flashforge networking library"), "", "",
                            "Networking library|*.dll;*.so;*.dylib", wxFD_OPEN | wxFD_FILE_MUST_EXIST);
        if (plugin.ShowModal() != wxID_OK) return;
        wxFileDialog settings(this, _L("Choose the matching Flashforge server settings"), "", "FLASHNETWORK9.DAT",
                              "Flashforge settings|*.DAT", wxFD_OPEN | wxFD_FILE_MUST_EXIST);
        if (settings.ShowModal() != wxID_OK) return;
        const std::string library_path = plugin.GetPath().ToUTF8().data();
        const std::string settings_path = settings.GetPath().ToUTF8().data();
        wxGetApp().app_config->set("flashforge_account_plugin", library_path);
        wxGetApp().app_config->set("flashforge_account_settings", settings_path);
        start([library_path, settings_path] {
            // Release previous global plugin initialization before loading again.
            flashforge_account.reset();
            flashforge_account = FlashforgeAccount::load(library_path, settings_path);
            return flashforge_account->state() != FlashforgeAccount::State::unavailable;
        }, true);
    }
};
}
FlashforgeAccountSnapshot flashforge_account_snapshot()
{
    wxASSERT(wxIsMainThread());
    poll_background_refresh();
    auto result = flashforge_snapshot;
    result.busy = flashforge_closing || wxGetApp().is_closing() || flashforge_dialog_open || flashforge_work.valid();
    return result;
}

bool flashforge_account_busy()
{
    return flashforge_account_snapshot().busy;
}

FlashforgePickerSnapshot flashforge_picker_snapshot()
{
    const auto snapshot = flashforge_account_snapshot();
    FlashforgePickerSnapshot result;
    result.signed_in = snapshot.state == FlashforgeAccount::State::signed_in && !snapshot.busy;
    if (!result.signed_in) return result;
    result.devices = snapshot.devices;
    result.observed_at = snapshot.observed_at;
    result.selection_is_current = [generation = snapshot.generation](const std::string& id) {
        const auto current = flashforge_account_snapshot();
        // This permits read-only navigation/profile binding, not print readiness.
        // Keep observation age available for UI and future print-specific gates.
        return !current.busy && current.state == FlashforgeAccount::State::signed_in &&
            current.generation == generation && !current.refresh_failed &&
            current.observed_at != std::chrono::steady_clock::time_point{} &&
            std::any_of(current.devices.begin(), current.devices.end(), [&id](const FlashforgeDevice& device) { return device.id == id; });
    };
    return result;
}

bool refresh_flashforge_devices()
{
    wxASSERT(wxIsMainThread());
    poll_background_refresh();
    if (flashforge_closing || wxGetApp().is_closing() || flashforge_dialog_open || flashforge_work.valid() || !flashforge_account ||
        flashforge_account->state() != FlashforgeAccount::State::signed_in) return false;
    if (wxGetApp().app_config->get_stealth_mode()) {
        flashforge_snapshot.message = "Stealth Mode is enabled. Disable it before refreshing Flashforge.";
        return false;
    }
    invalidate_flashforge_snapshot();
    try {
        flashforge_work = std::async(std::launch::async, [] { return flashforge_account->refresh_devices(); });
        flashforge_operation = ExternalOperation::list;
        flashforge_external_work = true;
        return true;
    } catch (const std::exception&) {
        flashforge_snapshot.message = "The account worker could not start. Please try again.";
        return false;
    }
}

bool refresh_flashforge_status(const std::string& id)
{
    wxASSERT(wxIsMainThread());
    poll_background_refresh();
    if (flashforge_closing || wxGetApp().is_closing() || flashforge_dialog_open || flashforge_work.valid() ||
        !flashforge_account || flashforge_account->state() != FlashforgeAccount::State::signed_in) return false;
    if (wxGetApp().app_config->get_stealth_mode()) {
        flashforge_snapshot.message = "Disable Stealth Mode to read live Flashforge cloud status.";
        return false;
    }
    try {
        flashforge_work = std::async(std::launch::async, [id] { return flashforge_account->refresh_status(id); });
        flashforge_operation = ExternalOperation::status;
        flashforge_external_work = true;
        return true;
    } catch (const std::exception&) {
        flashforge_snapshot.message = "The status worker could not start. Please try again.";
        return false;
    }
}

bool send_flashforge_cloud(VendorPrinting::CloudPrintRequest request, VendorPrinting::CloudFileInspector inspector)
{
    wxASSERT(wxIsMainThread());
    poll_background_refresh();
    if (flashforge_closing || wxGetApp().is_closing() || flashforge_dialog_open || flashforge_work.valid() ||
        !flashforge_account || flashforge_account->state() != FlashforgeAccount::State::signed_in ||
        wxGetApp().app_config->get_stealth_mode()) return false;
    auto cancelled = std::make_shared<std::atomic<bool>>(false);
    auto result = std::make_shared<VendorPrinting::CloudPrintResult>();
    try {
        flashforge_work = std::async(std::launch::async,
            [request = std::move(request), inspector = std::move(inspector), cancelled, result]() mutable {
                try { *result = flashforge_account->send_cloud(std::move(request), std::move(inspector), *cancelled); }
                catch (...) {
                    result->outcome = VendorPrinting::CloudPrintOutcome::needs_operator_review;
                    result->message = "The delivery result could not be confirmed. Check the printer before retrying.";
                }
                return result->outcome == VendorPrinting::CloudPrintOutcome::delivered;
            });
        flashforge_cancel = std::move(cancelled);
        flashforge_print_result = std::move(result);
        flashforge_operation = ExternalOperation::print;
        flashforge_external_work = true;
        flashforge_snapshot.cloud_job_running = true;
        flashforge_snapshot.cloud_job_id.clear();
        flashforge_snapshot.cloud_job_message = "Sending the selected plate through Flashforge Cloud…";
        return true;
    } catch (const std::exception&) {
        flashforge_snapshot.cloud_job_message = "The cloud print worker could not start. No request was sent.";
        return false;
    }
}

void cancel_flashforge_cloud()
{
    wxASSERT(wxIsMainThread());
    if (flashforge_snapshot.cloud_job_running && flashforge_cancel) flashforge_cancel->store(true);
}

void drain_flashforge_refresh()
{
    wxASSERT(wxIsMainThread());
    // MainFrame also shuts down during language recreation. Block reentrant
    // starts for this drain, retaining the block only for actual app shutdown.
    const bool previous = std::exchange(flashforge_closing, true);
    try {
        if (flashforge_dialog_drain) {
            flashforge_dialog_drain();
        } else if (flashforge_external_work && !flashforge_dialog_open && flashforge_work.valid()) {
            flashforge_work.wait();
            poll_background_refresh();
        }
    } catch (...) {
        flashforge_closing = previous || wxGetApp().is_closing();
        throw;
    }
    flashforge_closing = previous || wxGetApp().is_closing();
}

void show_vendor_accounts(wxWindow* parent,
    const std::function<void(const std::string&)>& on_open_flashforge_device)
{
    wxASSERT(wxIsMainThread());
    poll_background_refresh();
    if (flashforge_closing || wxGetApp().is_closing() || flashforge_dialog_open) return;
    if (flashforge_work.valid()) {
        wxMessageBox(_L("A Flashforge printer refresh is running. Wait for it to finish, then open Printer accounts."),
                     _L("Printer accounts"), wxOK | wxICON_INFORMATION, parent);
        return;
    }
    std::string selected;
    flashforge_dialog_open = true;
    try {
        VendorAccountsDialog dialog(parent);
        dialog.register_shutdown_drain();
        dialog.ShowModal();
        selected = dialog.selected_device();
    } catch (...) {
        flashforge_dialog_open = false;
        throw;
    }
    flashforge_dialog_open = false;
    if (!selected.empty()) {
        if (on_open_flashforge_device) on_open_flashforge_device(selected);
        else open_flashforge_device(selected);
    }
}
}} // namespace Slic3r::GUI
