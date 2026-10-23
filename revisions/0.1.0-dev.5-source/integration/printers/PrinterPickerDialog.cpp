#include "PrinterPickerDialog.hpp"
#include "PrinterCatalog.hpp"
#include "libslic3r/AppConfig.hpp"
#include "libslic3r/Config.hpp"
#include "libslic3r/Preset.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "slic3r/GUI/ConfigWizard.hpp"
#include "slic3r/GUI/DeviceManager.hpp"
#include "slic3r/GUI/DeviceCore/DevConfigUtil.h"
#include "slic3r/GUI/DeviceCore/DevExtruderSystem.h"
#include "slic3r/GUI/DeviceCore/DevManager.h"
#include "slic3r/GUI/DeviceCore/DevNozzleSystem.h"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/Lazy.hpp"
#include "slic3r/GUI/Monitor.hpp"
#include "slic3r/GUI/PhysicalPrinterDialog.hpp"
#include "slic3r/GUI/Tab.hpp"
#include "slic3r/Utils/CloudProvider.hpp"

#include <wx/button.h>
#include <wx/choice.h>
#include <wx/defs.h>
#include <wx/dialog.h>
#include <wx/event.h>
#include <wx/gdicmn.h>
#include <wx/listbox.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/string.h>
#include <wx/toplevel.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace Slic3r { namespace GUI {
namespace {

std::vector<double> reported_nozzles(MachineObject& machine)
{
    const DevNozzleSystem* nozzles = machine.GetNozzleSystem();
    const DevExtderSystem* extruders = machine.GetExtderSystem();
    if (!nozzles || !extruders || extruders->GetTotalExtderSize() <= 0) return {};
    std::map<int, double> logical_nozzles;
    for (const auto& pair : nozzles->GetExtNozzles()) {
        const DevNozzle& nozzle = pair.second;
        const double diameter = nozzle.GetNozzleDiameter();
        if (!nozzle.IsInfoReliable() || !std::isfinite(diameter) || diameter <= 0.) return {};
        if (!logical_nozzles.emplace(nozzle.GetLogicExtruderId(), diameter).second) return {};
    }
    const int count = extruders->GetTotalExtderSize();
    if (logical_nozzles.size() != static_cast<std::size_t>(count)) return {};
    std::vector<double> result;
    for (int i = 0; i < count; ++i) {
        const auto it = logical_nozzles.find(i);
        if (it == logical_nozzles.end()) return {};
        result.push_back(it->second);
    }
    return result;
}

std::vector<CatalogProfile> loaded_profiles()
{
    std::vector<CatalogProfile> result;
    const PresetBundle* bundle = wxGetApp().preset_bundle;
    if (!bundle) return result;
    for (const Preset& preset : bundle->printers.get_presets()) {
        // Orca's loader does not instantiate abstract machine/template records.
        if (!preset.loaded || !preset.is_visible || preset.is_default) continue;
        const auto* model = preset.config.option<ConfigOptionString>("printer_model");
        const auto* nozzles = preset.config.option<ConfigOptionFloats>("nozzle_diameter");
        if (!model || model->value.empty() || !nozzles || nozzles->values.empty()) continue;
        PrinterProvider provider;
        if ((preset.vendor && preset.vendor->id == "BBL") || model->value.rfind("Bambu Lab ", 0) == 0)
            provider = PrinterProvider::bambu;
        else if ((preset.vendor && preset.vendor->id == "Flashforge") || model->value.rfind("Flashforge ", 0) == 0)
            provider = PrinterProvider::flashforge;
        else continue;
        result.push_back({provider, preset.name, model->value, nozzles->values});
    }
    return result;
}

wxString device_readiness(PrinterReadiness readiness)
{
    switch (readiness) {
    case PrinterReadiness::ready: return _L("Ready");
    case PrinterReadiness::offline: return _L("Offline");
    case PrinterReadiness::busy: return _L("Busy");
    case PrinterReadiness::error: return _L("Device error");
    case PrinterReadiness::unknown: return _L("Status unavailable");
    }
    return _L("Status unavailable");
}

class PrinterPickerDialog final : public wxDialog {
public:
    PrinterPickerDialog(wxWindow* parent, const FlashforgePickerSnapshot& flashforge, bool can_open_accounts)
        : wxDialog(parent, wxID_ANY, _L("Choose printer"), wxDefaultPosition,
                   wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
    {
        auto* layout = new wxBoxSizer(wxVERTICAL);
        auto* introduction = new wxStaticText(this, wxID_ANY,
            _L("Choose a printer, then its exact nozzle profile. Offline profiles can be selected for editing."));
        introduction->Wrap(FromDIP(560));
        layout->Add(introduction, 0, wxEXPAND | wxALL, FromDIP(12));
        m_devices = new wxListBox(this, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(560), FromDIP(190)));
        layout->Add(m_devices, 1, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(12));
        auto* profile_header = new wxBoxSizer(wxHORIZONTAL);
        profile_header->Add(new wxStaticText(this, wxID_ANY, _L("Printer profile / nozzle")),
                            1, wxALIGN_CENTER_VERTICAL);
        auto* add_profiles = new wxButton(this, wxID_ADD, _L("Add printer profiles…"));
        add_profiles->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { EndModal(wxID_ADD); });
        profile_header->Add(add_profiles, 0, wxLEFT, FromDIP(8));
        layout->Add(profile_header, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(12));
        m_profiles = new wxChoice(this, wxID_ANY);
        layout->Add(m_profiles, 0, wxEXPAND | wxALL, FromDIP(12));
        m_note = new wxStaticText(this, wxID_ANY, _L("Select a printer and profile."));
        m_note->SetMinSize(wxSize(FromDIP(560), FromDIP(100)));
        layout->Add(m_note, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(12));
        m_lan_setup = new wxButton(this, wxID_ANY, _L("Set up Flashforge LAN printing…"));
        m_lan_setup->Enable(false);
        m_lan_setup->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { select(true); });
        layout->Add(m_lan_setup, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(12));
        auto* buttons = new wxBoxSizer(wxHORIZONTAL);
        if (can_open_accounts) {
            auto* accounts = new wxButton(this, wxID_HELP, _L("Printer accounts…"));
            accounts->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { EndModal(wxID_HELP); });
            buttons->Add(accounts, 0, wxRIGHT, FromDIP(8));
        }
        buttons->AddStretchSpacer();
        buttons->Add(new wxButton(this, wxID_CANCEL, _L("Cancel")), 0, wxRIGHT, FromDIP(8));
        m_use = new wxButton(this, wxID_OK, _L("Use printer / profile"));
        m_use->Enable(false);
        buttons->Add(m_use);
        layout->Add(buttons, 0, wxEXPAND | wxALL, FromDIP(12));
        m_devices->Bind(wxEVT_LISTBOX, [this](wxCommandEvent&) { update_profiles(); });
        m_profiles->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { update_note(); });
        m_use->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { select(); });
        populate(flashforge);
        SetSizerAndFit(layout);
        wxGetApp().UpdateDlgDarkUI(this);
        CentreOnParent();
    }

    const std::string& lan_setup_profile() const { return m_lan_setup_profile; }
    const std::string& lan_setup_printer() const { return m_lan_setup_printer; }
    const std::string& selected_flashforge() const { return m_selected_flashforge; }
    bool selected_bambu() const { return m_selected_bambu; }

private:
    PrinterCatalog m_catalog;
    std::vector<PrinterEntry> m_rows;
    std::vector<CatalogProfile> m_profile_values;
    std::vector<std::string> m_profile_ids;
    std::map<std::string, wxString> m_flashforge_status;
    std::function<bool(const std::string&)> m_flashforge_selection_is_current;
    wxListBox* m_devices = nullptr;
    wxChoice* m_profiles = nullptr;
    wxStaticText* m_note = nullptr;
    wxButton* m_use = nullptr;
    wxButton* m_lan_setup = nullptr;
    std::string m_lan_setup_profile;
    std::string m_lan_setup_printer;
    std::string m_selected_flashforge;
    bool m_selected_bambu = false;

    void populate(const FlashforgePickerSnapshot& flashforge)
    {
        m_profile_values = loaded_profiles();
        m_flashforge_selection_is_current = flashforge.selection_is_current;
        const auto now = PrinterCatalog::Clock::now();
        auto* manager = wxGetApp().getDeviceManager();
        const bool bambu_signed_in = wxGetApp().is_user_login(BBL_CLOUD_PROVIDER);
        m_catalog.set_account_readiness(PrinterProvider::bambu,
            bambu_signed_in ? AccountReadiness::signed_in : AccountReadiness::signed_out);
        if (manager && bambu_signed_in) {
            std::vector<CatalogPrinter> values;
            for (const auto& pair : manager->get_my_cloud_machine_list("bbl")) {
                MachineObject* machine = pair.second;
                if (!machine || machine->printer_agent_id != "bbl") continue;
                CatalogPrinter printer;
                printer.native_id = machine->get_dev_id();
                printer.name = machine->get_dev_name();
                printer.model = DevPrinterConfigUtil::get_printer_display_name(machine->get_show_printer_type());
                if (printer.model.empty()) printer.model = machine->get_show_printer_type();
                printer.nozzle_diameters = reported_nozzles(*machine);
                if (!machine->is_online()) printer.readiness = PrinterReadiness::offline;
                else if (machine->can_print()) printer.readiness = PrinterReadiness::ready;
                else if (machine->is_in_printing() || machine->is_in_prepare()) printer.readiness = PrinterReadiness::busy;
                printer.capabilities.wireless_print = CapabilitySupport::supported;
                printer.send_route_available = manager->get_current_printer_agent_id() == "bbl";
                values.push_back(printer);
            }
            m_catalog.replace(PrinterProvider::bambu, values, now, std::chrono::seconds(30));
        }
        m_catalog.set_account_readiness(PrinterProvider::flashforge,
            flashforge.signed_in ? AccountReadiness::signed_in : AccountReadiness::signed_out);
        if (flashforge.signed_in) {
            std::vector<CatalogPrinter> values;
            const std::string prefix = "flashforge:";
            for (const FlashforgeDevice& device : flashforge.devices) {
                if (device.id.rfind(prefix, 0) != 0) continue;
                CatalogPrinter printer;
                printer.native_id = device.id.substr(prefix.size());
                printer.name = device.name;
                printer.model = device.model;
                wxString status = wxString::FromUTF8(device.status);
                status.Replace("\r", " ");
                status.Replace("\n", " ");
                status.Replace("\t", " ");
                if (status.length() > 160) status = status.Left(160) + "…";
                m_flashforge_status[device.id] = status;
                // The account adapter has no verified send/status transport yet.
                values.push_back(printer);
            }
            m_catalog.replace(PrinterProvider::flashforge, values, flashforge.observed_at, std::chrono::seconds(30));
        }
        m_rows = m_catalog.entries();
        for (const PrinterEntry& entry : m_rows) {
            wxString status = device_readiness(entry.printer.readiness);
            const auto raw_status = m_flashforge_status.find(entry.id);
            if (raw_status != m_flashforge_status.end() && !raw_status->second.empty()) status = raw_status->second;
            wxString label = wxString::FromUTF8(printer_provider_name(entry.provider)) + " | " +
                wxString::FromUTF8(entry.printer.name.empty() ? entry.printer.native_id : entry.printer.name) + " | " +
                wxString::FromUTF8(canonical_printer_model(entry.provider, entry.printer.model)) + " | " +
                status;
            m_devices->Append(label);
        }
        if (m_rows.empty()) {
            set_note(_L("No cached account printers are available. Use Printer accounts to sign in or refresh Flashforge printers. For Bambu, select a Bambu profile and open Orca's Device tab to refresh its printer list. Existing LAN printers remain available there."));
            return;
        }
        int selection = 0;
        if (wxGetApp().app_config) {
            const std::string saved = wxGetApp().app_config->get("dual_vendor_selected_printer");
            for (std::size_t i = 0; i < m_rows.size(); ++i)
                if (m_rows[i].id == saved) selection = static_cast<int>(i);
        }
        m_devices->SetSelection(selection);
        update_profiles();
    }

    const PrinterEntry* selected_entry() const
    {
        const int index = m_devices->GetSelection();
        return index >= 0 && static_cast<std::size_t>(index) < m_rows.size() ? &m_rows[static_cast<std::size_t>(index)] : nullptr;
    }

    std::string selected_profile() const
    {
        const int index = m_profiles->GetSelection();
        return index >= 0 && static_cast<std::size_t>(index) < m_profile_ids.size()
            ? m_profile_ids[static_cast<std::size_t>(index)] : std::string();
    }

    void set_note(const wxString& text)
    {
        m_note->SetLabel(text);
        m_note->Wrap(FromDIP(560));
        Layout();
    }

    void update_profiles()
    {
        m_profiles->Clear();
        m_profile_ids.clear();
        const PrinterEntry* entry = selected_entry();
        if (entry) {
            const ProfileChoice choice = m_catalog.choose_profile(entry->id, m_profile_values);
            m_profile_ids = choice.candidates;
            for (const std::string& name : m_profile_ids) m_profiles->Append(wxString::FromUTF8(name));
            m_profiles->SetSelection(wxNOT_FOUND);
            if (choice.state == ProfileChoiceState::selected) m_profiles->SetSelection(0);
        }
        // An ambiguous nozzle always requires a fresh, explicit selection.
        update_note();
    }

    void update_note()
    {
        const PrinterEntry* entry = selected_entry();
        const std::string profile = selected_profile();
        m_use->Enable(entry && !profile.empty());
        m_lan_setup->Enable(entry && entry->provider == PrinterProvider::flashforge && !profile.empty());
        if (!entry) { set_note(_L("Select a printer and profile.")); return; }
        wxString note;
        if (m_profile_ids.empty()) {
            note = _L("No enabled profile matches this printer. Click Add printer profiles to enable its exact machine and nozzle variant, then reopen this picker.");
        } else if (profile.empty()) {
            note = _L("Choose the exact installed nozzle profile. Multiple matching profiles are available.");
        } else if (entry->provider == PrinterProvider::flashforge) {
            note = _L("This selects your Flashforge profile. LAN setup opens Orca's Connection settings: choose the Flashforge host type, then discover the printer or enter its address, serial number and checkCode/API key. Flashforge cloud job submission is not connected.");
        } else if (entry->printer.readiness != PrinterReadiness::ready) {
            note = _L("The profile can be selected for editing. Orca's normal send workflow checks the printer's live connection and readiness before printing.");
        } else {
            note = _L("This uses Orca's existing Bambu printer selection and print workflow. No print job is started by this picker.");
        }
        const auto now = PrinterCatalog::Clock::now();
        if (entry->observed_at == PrinterCatalog::Clock::time_point{} || now < entry->observed_at)
            note += "\n" + _L("Printer list capture time is unavailable.");
        else {
            const auto age = std::chrono::duration_cast<std::chrono::seconds>(now - entry->observed_at).count();
            note += "\n" + wxString::Format(_L("Printer list captured %lld seconds ago."), static_cast<long long>(age));
        }
        set_note(note);
    }

    bool profile_is_current(const PrinterEntry& entry, const std::string& profile) const
    {
        const PresetBundle* bundle = wxGetApp().preset_bundle;
        if (!bundle) return false;
        const Preset& edited = bundle->printers.get_edited_preset();
        if (edited.name != profile) return false;
        const auto* model = edited.config.option<ConfigOptionString>("printer_model");
        const auto* nozzles = edited.config.option<ConfigOptionFloats>("nozzle_diameter");
        if (!model || !nozzles) return false;
        return m_catalog.profile_selection_is_current(entry.id, profile,
            CatalogProfile{entry.provider, edited.name, model->value, nozzles->values}, loaded_profiles());
    }

    void select(bool lan_setup = false)
    {
        const PrinterEntry* entry = selected_entry();
        const std::string profile = selected_profile();
        if (!entry || profile.empty() ||
            m_catalog.choose_profile(entry->id, m_profile_values, profile).state != ProfileChoiceState::selected) return;
        if (lan_setup && entry->provider != PrinterProvider::flashforge) return;
        auto* manager = wxGetApp().getDeviceManager();
        if (entry->provider == PrinterProvider::bambu) {
            if (!manager || !wxGetApp().is_user_login(BBL_CLOUD_PROVIDER)) {
                set_note(_L("The Bambu account changed. Reopen this picker after signing in to Bambu."));
                return;
            }
            const auto machines = manager->get_my_cloud_machine_list("bbl");
            const auto found = machines.find(entry->printer.native_id);
            if (found == machines.end() || !found->second || found->second->printer_agent_id != "bbl") {
                set_note(_L("This printer is no longer in the current Bambu account. Reopen the picker to refresh its list."));
                return;
            }
        }
        Tab* tab = wxGetApp().get_tab(Preset::TYPE_PRINTER);
        if (!tab || !tab->select_preset(profile)) return;
        // Tab::select_preset reports cancellation, not whether lookup succeeded.
        // Recheck the real edited preset and current metadata after nested prompts.
        if (!profile_is_current(*entry, profile)) {
            m_profile_values = loaded_profiles();
            update_profiles();
            set_note(_L("The requested profile changed or disappeared while selecting it. No printer binding was saved. Choose an exact profile again."));
            return;
        }
        if (entry->provider == PrinterProvider::bambu) {
            // A nested unsaved-settings prompt may let device/account events run.
            manager = wxGetApp().getDeviceManager();
            bool selected = false;
            if (manager && manager->get_current_printer_agent_id() == "bbl" &&
                wxGetApp().is_user_login(BBL_CLOUD_PROVIDER)) {
                const auto machines = manager->get_my_cloud_machine_list("bbl");
                const auto found = machines.find(entry->printer.native_id);
                if (found != machines.end() && found->second && found->second->printer_agent_id == "bbl") {
                    if (MonitorPanel* monitor = MonitorPanel::if_built()) {
                        wxCommandEvent event(wxEVT_COMMAND_CHOICE_SELECTED);
                        event.SetString(wxString::FromUTF8(entry->printer.native_id));
                        monitor->on_select_printer(event);
                        MachineObject* machine = manager->get_selected_machine();
                        selected = machine && machine->printer_agent_id == "bbl" && machine->get_dev_id() == entry->printer.native_id;
                    } else {
                        selected = manager->set_selected_machine(entry->printer.native_id);
                        if (selected) MonitorPanel::on_machine_selected(manager->get_selected_machine());
                    }
                }
            }
            if (!selected) {
                wxMessageBox(_L("The profile was selected, but the Bambu printer could not be selected. Use Orca's Device tab to reconnect it."),
                             _L("Printer selection"), wxOK | wxICON_INFORMATION, this);
                EndModal(wxID_OK);
                return;
            }
        } else if (m_flashforge_selection_is_current && !m_flashforge_selection_is_current(entry->id)) {
            wxMessageBox(_L("The profile was selected for editing, but the Flashforge account or printer list changed. Reopen Printer accounts before selecting this printer again."),
                         _L("Printer selection"), wxOK | wxICON_INFORMATION, this);
            EndModal(wxID_OK);
            return;
        }
        if (wxGetApp().app_config) {
            wxGetApp().app_config->set("dual_vendor_selected_printer", entry->id);
            if (entry->provider == PrinterProvider::flashforge)
                wxGetApp().app_config->set("flashforge_selected_printer", entry->id);
        }
        if (entry->provider == PrinterProvider::flashforge) m_selected_flashforge = entry->id;
        else m_selected_bambu = true;
        if (lan_setup) {
            m_lan_setup_profile = profile;
            m_lan_setup_printer = entry->id;
            EndModal(wxID_APPLY);
        } else EndModal(wxID_OK);
    }
};

} // namespace

void show_printer_picker(wxWindow* parent, const FlashforgePickerSnapshot& flashforge,
                        const std::function<void()>& open_accounts,
                        const std::function<void(const std::string&)>& on_select_flashforge,
                        const std::function<void()>& on_select_bambu)
{
    bool accounts_requested = false;
    bool profiles_requested = false;
    std::string lan_setup_profile;
    std::string lan_setup_printer;
    std::string selected_flashforge;
    bool selected_bambu = false;
    {
        PrinterPickerDialog dialog(parent, flashforge, static_cast<bool>(open_accounts));
        const int result = dialog.ShowModal();
        accounts_requested = result == wxID_HELP;
        profiles_requested = result == wxID_ADD;
        selected_flashforge = dialog.selected_flashforge();
        selected_bambu = dialog.selected_bambu();
        if (result == wxID_APPLY) {
            lan_setup_profile = dialog.lan_setup_profile();
            lan_setup_printer = dialog.lan_setup_printer();
        }
    }
    if (profiles_requested) {
        wxGetApp().run_wizard(ConfigWizard::RR_USER, ConfigWizard::SP_PRINTERS);
        return;
    }
    if (accounts_requested && open_accounts) open_accounts();
    if (!lan_setup_profile.empty()) {
        const PresetBundle* bundle = wxGetApp().preset_bundle;
        if (!bundle || bundle->printers.get_edited_preset().name != lan_setup_profile ||
            (flashforge.selection_is_current && !flashforge.selection_is_current(lan_setup_printer))) return;
        // This is the same public Connection dialog used by Plater's titlebar.
        // It retains native host choices and saving; no automatic discovery/send.
        PhysicalPrinterDialog setup(parent);
        setup.ShowModal();
    }
    if (!selected_flashforge.empty() && on_select_flashforge &&
        (!flashforge.selection_is_current || flashforge.selection_is_current(selected_flashforge)))
        on_select_flashforge(selected_flashforge);
    if (selected_bambu && on_select_bambu) on_select_bambu();
}

}} // namespace Slic3r::GUI
