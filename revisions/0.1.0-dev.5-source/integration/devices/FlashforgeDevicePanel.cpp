#include "FlashforgeDevicePanel.hpp"
#include "FlashforgeDeviceStatus.hpp"
#include "FlashforgeCloudSendDialog.hpp"
#include "../FlashforgeAccount.hpp"
#include "../VendorAccountsDialog.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/defs.h>
#include <wx/event.h>
#include <wx/gdicmn.h>
#include <wx/listbox.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/string.h>
#include <wx/timer.h>
#include <wx/weakref.h>
#include <wx/window.h>
#include <chrono>
#include <cstddef>
#include <exception>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Slic3r { namespace GUI {
namespace {
constexpr const char* panel_name = "flashforge_device_panel";

wxString display_text(std::string text)
{
    for (char& c : text) if (static_cast<unsigned char>(c) < 32 || static_cast<unsigned char>(c) == 127) c = ' ';
    if (text.size() > 1024) {
        std::size_t boundary = 1024;
        while (boundary > 0 && (static_cast<unsigned char>(text[boundary]) & 0xc0) == 0x80) --boundary;
        text.resize(boundary);
    }
    return wxString::FromUTF8(text);
}

wxString state_text(FlashforgeDeviceViewState state)
{
    switch (state) {
    case FlashforgeDeviceViewState::unavailable: return _L("Open Printer accounts to connect Flashforge.");
    case FlashforgeDeviceViewState::signed_out: return _L("Sign in to Flashforge to view its printers.");
    case FlashforgeDeviceViewState::loading: return _L("Waiting for the Flashforge printer list to refresh…");
    case FlashforgeDeviceViewState::refresh_failed: return _L("The printer list could not be refreshed. Any displayed values are from the previous account-list snapshot.");
    case FlashforgeDeviceViewState::no_printers: return _L("No printers were returned by this Flashforge account.");
    case FlashforgeDeviceViewState::choose_printer: return _L("Choose a Flashforge printer to view its last reported status.");
    case FlashforgeDeviceViewState::selected: return _L("Showing the selected printer's account-list snapshot.");
    case FlashforgeDeviceViewState::invalid_snapshot: return _L("The Flashforge printer list could not be verified. Refresh the list or reopen Printer accounts.");
    }
    return _L("Flashforge status is unavailable.");
}

class FlashforgeDevicePanel final : public wxPanel {
public:
    FlashforgeDevicePanel(wxWindow* parent, FlashforgeDeviceCallbacks callbacks)
        : wxPanel(parent, wxID_ANY), m_callbacks(std::move(callbacks)), m_timer(this)
    {
        SetName(panel_name);
        auto* layout = new wxBoxSizer(wxVERTICAL);
        layout->Add(new wxStaticText(this, wxID_ANY, _L("Flashforge Device")), 0, wxALL, FromDIP(12));
        auto* source = new wxStaticText(this, wxID_ANY,
            _L("Printer list and live device status use your Flashforge cloud account."));
        source->Wrap(FromDIP(620));
        layout->Add(source, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(12));
        m_status = new wxStaticText(this, wxID_ANY, "");
        layout->Add(m_status, 0, wxEXPAND | wxALL, FromDIP(12));
        m_service_message = new wxStaticText(this, wxID_ANY, "");
        layout->Add(m_service_message, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(12));
        m_printers = new wxListBox(this, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(560), FromDIP(145)));
        layout->Add(m_printers, 1, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(12));
        auto* details = new wxFlexGridSizer(2, FromDIP(8), FromDIP(18));
        details->AddGrowableCol(1);
        add_detail(details, _L("Printer"), m_name);
        add_detail(details, _L("Model"), m_model);
        add_detail(details, _L("Last reported status"), m_reported_status);
        add_detail(details, _L("List refreshed"), m_observed);
        add_detail(details, _L("Device status"), m_live_status);
        add_detail(details, _L("Nozzle / target"), m_nozzle);
        add_detail(details, _L("Bed / target"), m_bed);
        add_detail(details, _L("Print progress"), m_progress);
        add_detail(details, _L("Firmware"), m_firmware);
        add_detail(details, _L("Device updated"), m_live_age);
        layout->Add(details, 0, wxEXPAND | wxALL, FromDIP(12));
        m_monitor = new wxCheckBox(this, wxID_ANY, _L("Monitor selected printer through Flashforge Cloud"));
        layout->Add(m_monitor, 0, wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(12));
        m_cloud_job = new wxStaticText(this, wxID_ANY, "");
        layout->Add(m_cloud_job, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(12));
        auto* buttons = new wxBoxSizer(wxHORIZONTAL);
        m_refresh = new wxButton(this, wxID_REFRESH, _L("Refresh printer list"));
        auto* accounts = new wxButton(this, wxID_ANY, _L("Printer accounts…"));
        buttons->Add(m_refresh, 0, wxRIGHT, FromDIP(8));
        buttons->Add(accounts);
        m_send = new wxButton(this, wxID_ANY, _L("Send current plate via cloud…"));
        m_cancel_send = new wxButton(this, wxID_ANY, _L("Cancel sending"));
        buttons->Add(m_send, 0, wxLEFT, FromDIP(8));
        buttons->Add(m_cancel_send, 0, wxLEFT, FromDIP(8));
        layout->Add(buttons, 0, wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(12));
        accounts->Enable(static_cast<bool>(m_callbacks.open_accounts));
        SetSizerAndFit(layout);
        m_printers->Bind(wxEVT_LISTBOX, [this](wxCommandEvent&) {
            const int row = m_printers->GetSelection();
            if (row >= 0 && static_cast<std::size_t>(row) < m_rendered_ids.size()) {
                m_model_value.select(m_rendered_ids[static_cast<std::size_t>(row)]);
                render();
            }
        });
        m_refresh->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            const auto refresh = m_callbacks.refresh;
            wxWeakRef<FlashforgeDevicePanel> panel(this);
            if (refresh) refresh();
            // A refusal (for example Stealth Mode) updates the copied service
            // message. The callback may destroy this panel, so reacquire it.
            if (panel && !panel->IsBeingDeleted()) panel->poll();
        });
        accounts->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            const auto open_accounts = m_callbacks.open_accounts;
            if (open_accounts) open_accounts();
        });
        m_send->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            const auto id = m_model_value.selected_id();
            m_monitor->SetValue(false);
            wxWeakRef<FlashforgeDevicePanel> panel(this);
            show_flashforge_cloud_send(this, id);
            if (panel && !panel->IsBeingDeleted() && flashforge_account_snapshot().cloud_job_running)
                panel->m_monitor->SetValue(true);
        });
        m_cancel_send->Bind(wxEVT_BUTTON, [](wxCommandEvent&) { cancel_flashforge_cloud(); });
        m_monitor->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) { m_next_live = {}; });
        Bind(wxEVT_TIMER, [this](wxTimerEvent&) { poll(); });
        poll();
        m_timer.Start(500);
    }
    ~FlashforgeDevicePanel() override { m_timer.Stop(); }

    bool select(const std::string& id)
    {
        poll();
        const bool selected = m_model_value.select(id);
        render();
        return selected;
    }
private:
    FlashforgeDeviceCallbacks m_callbacks;
    FlashforgeDeviceStatusModel m_model_value;
    wxTimer m_timer;
    wxListBox* m_printers = nullptr;
    wxStaticText *m_status = nullptr, *m_service_message = nullptr, *m_name = nullptr, *m_model = nullptr,
                 *m_reported_status = nullptr, *m_observed = nullptr;
    wxButton* m_refresh = nullptr;
    wxButton *m_send = nullptr, *m_cancel_send = nullptr;
    wxCheckBox* m_monitor = nullptr;
    wxStaticText *m_live_status = nullptr, *m_nozzle = nullptr, *m_bed = nullptr,
        *m_progress = nullptr, *m_firmware = nullptr, *m_live_age = nullptr, *m_cloud_job = nullptr;
    std::chrono::steady_clock::time_point m_next_live;
    FlashforgeAccountSnapshot m_snapshot;
    std::vector<std::string> m_rendered_ids;
    std::vector<wxString> m_rendered_labels;

    void add_detail(wxFlexGridSizer* layout, const wxString& label, wxStaticText*& value)
    {
        layout->Add(new wxStaticText(this, wxID_ANY, label));
        value = new wxStaticText(this, wxID_ANY, _L("Not reported"));
        layout->Add(value, 0, wxEXPAND);
    }
    void poll()
    {
        if (IsBeingDeleted() || wxGetApp().is_closing()) return;
        try {
            if (m_callbacks.read_snapshot) {
                m_snapshot = m_callbacks.read_snapshot();
                m_model_value.apply_snapshot(m_snapshot, FlashforgeDeviceStatusModel::Clock::now());
            }
            else m_model_value.source_unavailable();
        } catch (const std::exception&) { m_model_value.source_unavailable(); }
        render();
        const auto now = std::chrono::steady_clock::now();
        if (IsShownOnScreen() && m_monitor->GetValue() && !m_snapshot.busy &&
            !m_model_value.selected_id().empty() && now >= m_next_live) {
            m_next_live = now + std::chrono::seconds(5);
            refresh_flashforge_status(m_model_value.selected_id());
        }
    }
    void render()
    {
        const auto view = m_model_value.view(FlashforgeDeviceStatusModel::Clock::now());
        std::vector<std::string> ids;
        std::vector<wxString> labels;
        for (const auto& device : m_model_value.devices()) {
            ids.push_back(device.id);
            labels.push_back((device.name.empty() ? _L("Unnamed Flashforge printer") : display_text(device.name)) +
                             " | " + (device.model.empty() ? _L("Model not reported") : display_text(device.model)));
        }
        if (ids != m_rendered_ids || labels != m_rendered_labels) {
            m_printers->Clear();
            for (const auto& label : labels) m_printers->Append(label);
            m_rendered_ids = std::move(ids);
            m_rendered_labels = std::move(labels);
        }
        int selection = wxNOT_FOUND;
        for (std::size_t i = 0; i < m_rendered_ids.size(); ++i)
            if (m_rendered_ids[i] == m_model_value.selected_id()) selection = static_cast<int>(i);
        m_printers->SetSelection(selection);
        m_printers->Enable(!view.busy && !m_rendered_ids.empty());
        m_status->SetLabel(state_text(view.state));
        m_status->Wrap(FromDIP(620));
        m_service_message->SetLabel(display_text(view.message));
        m_service_message->Wrap(FromDIP(620));
        m_service_message->Show(!view.message.empty());
        m_name->SetLabel(view.has_selected_device
            ? (view.name.empty() ? _L("Unnamed Flashforge printer") : display_text(view.name)) : _L("No printer selected"));
        m_model->SetLabel(view.has_selected_device && !view.model.empty() ? display_text(view.model) : _L("Not reported"));
        m_reported_status->SetLabel(view.has_selected_device && !view.reported_status.empty()
            ? display_text(view.reported_status) : _L("Not reported"));
        m_observed->SetLabel(view.age
            ? wxString::Format(_L("%lld seconds ago"), static_cast<long long>(view.age->count())) : _L("No successful refresh yet"));
        m_refresh->Enable(view.can_refresh && static_cast<bool>(m_callbacks.refresh));
        const auto& live = m_snapshot.live;
        const bool selected_live = live && live->id == m_model_value.selected_id();
        const auto temperature = [](const std::optional<double>& actual, const std::optional<double>& target) {
            return actual && target ? wxString::Format("%.1f / %.1f °C", *actual, *target) : _L("Not reported");
        };
        m_live_status->SetLabel(selected_live ? display_text(live->status) : _L("Not connected"));
        m_nozzle->SetLabel(selected_live ? temperature(live->nozzle_temperature, live->nozzle_target) : _L("Not reported"));
        m_bed->SetLabel(selected_live ? temperature(live->bed_temperature, live->bed_target) : _L("Not reported"));
        m_progress->SetLabel(selected_live && live->progress ? wxString::Format("%.1f%%", *live->progress * 100.) : _L("Not reported"));
        m_firmware->SetLabel(selected_live ? display_text(live->firmware) : _L("Not reported"));
        const auto now = std::chrono::steady_clock::now();
        m_live_age->SetLabel(selected_live && m_snapshot.live_observed_at != std::chrono::steady_clock::time_point{} && now >= m_snapshot.live_observed_at
            ? wxString::Format(_L("%lld seconds ago"), static_cast<long long>(std::chrono::duration_cast<std::chrono::seconds>(now - m_snapshot.live_observed_at).count())) : _L("No device refresh yet"));
        m_send->Enable(!m_snapshot.busy && !m_model_value.selected_id().empty());
        m_cancel_send->Enable(m_snapshot.cloud_job_running);
        m_cloud_job->SetLabel(display_text(m_snapshot.cloud_job_message) +
            (m_snapshot.cloud_job_id.empty() ? wxString() : "\n" + _L("Cloud job") + ": " + display_text(m_snapshot.cloud_job_id)));
        m_cloud_job->Wrap(FromDIP(620));
        Layout();
    }
};
}

wxWindow* create_flashforge_device_panel(wxWindow* parent, FlashforgeDeviceCallbacks callbacks)
{
    return new FlashforgeDevicePanel(parent, std::move(callbacks));
}

bool select_flashforge_device(wxWindow* panel, const std::string& qualified_id)
{
    auto* device_panel = dynamic_cast<FlashforgeDevicePanel*>(panel);
    return device_panel && device_panel->select(qualified_id);
}

void open_flashforge_device(const std::string& qualified_id)
{
    MainFrame* frame = wxGetApp().mainframe;
    if (!frame || frame->IsBeingDeleted() || wxGetApp().is_closing()) return;
    frame->select_tab("flashforge_device");
    if (!qualified_id.empty())
        select_flashforge_device(wxWindow::FindWindowByName(panel_name, frame), qualified_id);
}

}} // namespace Slic3r::GUI
