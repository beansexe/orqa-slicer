#include "VendorAccountsDialog.hpp"
#include "FlashforgeAccount.hpp"
#include "FlashforgeSessionSettings.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/AppConfig.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/Utils/CloudProvider.hpp"
#include "slic3r/Utils/NetworkAgent.hpp"
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/defs.h>
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
#include <chrono>
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

class VendorAccountsDialog final : public wxDialog {
public:
    explicit VendorAccountsDialog(wxWindow* parent)
        : wxDialog(parent, wxID_ANY, _L("Printer accounts"), wxDefaultPosition,
                   wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER), m_timer(this)
    {
        m_service = "OrqaSlicer.Flashforge." + wxString::FromUTF8(data_dir());
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
        m_plugin = new wxButton(this, wxID_ANY, _L("Choose plugin"));
        for (auto* button : {m_login, m_logout, m_refresh, m_plugin})
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
                flashforge_account->login(username, password);
            }, false, true);
        });
        m_logout->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            if (!erase_secret()) return;
            start([] { flashforge_account->logout(); });
        });
        m_refresh->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            start([] { flashforge_account->refresh_devices(); });
        });
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
            start([library, settings] { flashforge_account = FlashforgeAccount::load(library, settings); }, true);
        }
    }
    ~VendorAccountsDialog() override
    {
        m_timer.Stop();
        // A non-vetoable app shutdown may destroy the dialog mid-call. Join the
        // worker before plugin/session destruction; worker never references UI.
        if (m_work.valid()) m_work.wait();
    }
private:
    wxTimer m_timer;
    std::future<void> m_work;
    bool m_restore_after_load = false;
    bool m_new_login = false;
    wxString m_service;
    wxStaticText *m_bambu_status = nullptr, *m_status = nullptr;
    wxTextCtrl *m_username = nullptr, *m_password = nullptr;
    wxCheckBox* m_remember = nullptr;
    wxListBox* m_devices = nullptr;
    wxButton *m_login = nullptr, *m_logout = nullptr, *m_refresh = nullptr,
             *m_plugin = nullptr, *m_close = nullptr;
    wxBoxSizer* m_bambu_buttons = nullptr;
    bool busy() const { return m_work.valid(); }

    void start(std::function<void()> operation, bool restore_after_load = false, bool new_login = false)
    {
        if (busy()) return;
        m_restore_after_load = restore_after_load;
        m_new_login = new_login;
        m_status->SetLabel(_L("Connecting…"));
        for (auto* control : {m_login, m_logout, m_refresh, m_plugin, m_close}) control->Disable();
        m_username->Disable();
        m_password->Disable();
        m_remember->Disable();
        // Disallow leaving through Bambu's buttons while the worker is active.
        for (const auto* item : m_bambu_buttons->GetChildren()) item->GetWindow()->Disable();
        try {
            m_work = std::async(std::launch::async, std::move(operation));
        } catch (const std::exception&) {
            m_restore_after_load = false;
            m_new_login = false;
            update();
            m_status->SetLabel(_L("The account worker could not start. Please try again."));
        }
    }
    void poll()
    {
        m_bambu_status->SetLabel(wxGetApp().is_user_login(BBL_CLOUD_PROVIDER)
            ? _L("Signed in to Bambu Lab") : _L("Signed out of Bambu Lab"));
        if (!busy()) return;
        if (m_work.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
        bool succeeded = true;
        try { m_work.get(); } catch (const std::exception&) { succeeded = false; }
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
        Layout();
    }
    void restore_or_refresh()
    {
        if (flashforge_account->state() == FlashforgeAccount::State::signed_in) {
            start([] { flashforge_account->refresh_devices(); });
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
            start([refresh = std::move(refresh)] { flashforge_account->restore(refresh); });
        }
    }
    bool persist_restoration_disabled(bool disabled)
    {
        if (persist_flashforge_restoration_disabled(*wxGetApp().app_config, disabled)) return true;
        wxMessageBox(_L("Flashforge account settings could not be saved. The account change has not been completed. Check that the slicer's data folder is writable, then try again."),
                     _L("Flashforge account"), wxOK | wxICON_ERROR, this);
        return false;
    }
    bool erase_secret()
    {
        // The durable flag is authoritative even if a locked credential store
        // cannot remove its token. Do not mistake a failed Load for absence.
        if (!persist_restoration_disabled(true)) return false;
        auto store = wxSecretStore::GetDefault();
        bool deletion_failed = !store.IsOk();
        if (!deletion_failed) {
            wxLogCollector errors;
            const bool deleted = store.Delete(m_service);
            // wxWidgets reports a missing credential silently, but logs an
            // actual deletion error on Windows, macOS and Linux.
            deletion_failed = !deleted && !errors.GetMessages().empty();
        }
        if (deletion_failed)
            wxMessageBox(_L("Automatic Flashforge sign-in has been disabled. The system credential store could not remove the saved sign-in; local sign-out can still continue."),
                         _L("Flashforge account"), wxOK | wxICON_INFORMATION, this);
        return true; // Restoration is disabled on disk; local logout is safe.
    }
    void save_secret(bool new_login = false)
    {
        if (!flashforge_account || !m_remember->GetValue()) return;
        const bool disabled = wxGetApp().app_config->get(flashforge_restoration_setting) == "1";
        if (flashforge_account->state() == FlashforgeAccount::State::signed_out &&
            flashforge_account->session_secret().empty()) {
            if (!disabled) erase_secret(); // Do not restore an explicitly invalidated token on restart.
            return;
        }
        if (flashforge_account->state() != FlashforgeAccount::State::signed_in) return;
        if (disabled && !new_login) {
            m_status->SetLabel(_L("Signed in for this session. Sign out and sign in again to keep this account remembered."));
            return;
        }
        auto store = wxSecretStore::GetDefault();
        wxSecretValue secret(wxString::FromUTF8(flashforge_account->session_secret()));
        if (!store.IsOk() || !store.Save(m_service, m_username->GetValue(), secret)) {
            m_status->SetLabel(_L("Signed in for this session. The system credential store could not save your sign-in."));
            return;
        }
        // A checkbox toggle or background refresh cannot revive an account
        // explicitly signed out earlier; only a saved new login may do so.
        if (new_login && !persist_restoration_disabled(false))
            m_status->SetLabel(_L("Signed in for this session. Automatic sign-in remains disabled because account settings could not be saved."));
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
        }, true);
    }
};
}
void show_vendor_accounts(wxWindow* parent)
{
    VendorAccountsDialog dialog(parent);
    dialog.ShowModal();
}
}} // namespace Slic3r::GUI
