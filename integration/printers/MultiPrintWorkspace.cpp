#include "MultiPrintWorkspace.hpp"
#include "PrinterCatalog.hpp"
#include "PrinterPickerDialog.hpp"
#include "../VendorAccountsDialog.hpp"
#include "../devices/FlashforgeCloudSendDialog.hpp"
#include "libslic3r/AppConfig.hpp"
#include "libslic3r/Config.hpp"
#include "libslic3r/Format/bbs_3mf.hpp"
#include "libslic3r/Preset.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Utils.hpp"
#include "slic3r/GUI/DeviceManager.hpp"
#include "slic3r/GUI/DeviceCore/DevManager.h"
#include "slic3r/GUI/Event.hpp"
#include "slic3r/GUI/GLToolbar.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/Monitor.hpp"
#include "slic3r/GUI/PartPlate.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/Utils/CloudProvider.hpp"
#include <wx/aui/framemanager.h>
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/dialog.h>
#include <wx/event.h>
#include <wx/font.h>
#include <wx/filedlg.h>
#include <wx/file.h>
#include <wx/gdicmn.h>
#include <wx/listbox.h>
#include <wx/msgdlg.h>
#include <wx/panel.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/string.h>
#include <wx/timer.h>
#include <algorithm>
#include <boost/filesystem/path.hpp>
#include <nlohmann/json.hpp>
#include <chrono>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <fstream>
#include <ios>
#include <memory>
#include <string>
#include <stdexcept>
#include <utility>
#include <vector>

namespace Slic3r { namespace GUI {
namespace {
class MultiPrintPanel;
MultiPrintPanel* workspace = nullptr;

struct Target {
    std::string id, label, profile, model;
    PrinterProvider provider;
    DynamicPrintConfig config, plate_config;
    int plate_index = 0;
    // Identity check without dereferencing an obsolete plate pointer.
    PartPlate* source = nullptr;
    wxString status = _L("Not sliced for this printer");
};

class MultiPrintPanel final : public wxScrolledWindow {
public:
    explicit MultiPrintPanel(Plater* plater) : wxScrolledWindow(plater), m_plater(plater) {
        SetScrollRate(0, FromDIP(12));
        auto* layout = new wxBoxSizer(wxVERTICAL);
        auto* title = new wxStaticText(this, wxID_ANY, _L("Multi-print plates"));
        title->SetFont(title->GetFont().Bold());
        layout->Add(title, 0, wxALL, FromDIP(10));
        auto* help = new wxStaticText(this, wxID_ANY,
            _L("Add a printer for the current plate. Select a card to edit its printer, filament and process settings using the usual controls. Objects remain shared."));
        help->Wrap(FromDIP(245));
        layout->Add(help, 0, wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(10));
        m_cards = new wxListBox(this, wxID_ANY, wxDefaultPosition, FromDIP(wxSize(260, 200)), 0, nullptr, wxLB_SINGLE | wxLB_HSCROLL);
        layout->Add(m_cards, 1, wxEXPAND | wxALL, FromDIP(8));
        m_note = new wxStaticText(this, wxID_ANY, _L("Choose the source plate, then add its first printer."));
        m_note->Wrap(FromDIP(245));
        layout->Add(m_note, 0, wxEXPAND | wxALL, FromDIP(10));
        add_button(layout, _L("+ Add printer / plate"), [this] { add_target(); });
        add_button(layout, _L("Remove selected card"), [this] { remove_target(); });
        add_button(layout, _L("Slice selected printer"), [this] { run_batch(true); });
        add_button(layout, _L("Print to all printers"), [this] { run_batch(); });
        add_button(layout, _L("Save Multi-print workspace…"), [this] { save_workspace(); });
        add_button(layout, _L("Load saved printer cards…"), [this] { load_cards(); });
        auto* footer = new wxStaticText(this, wxID_ANY,
            _L("Each printer is sliced separately. The batch opens each vendor's send confirmation. Check the printer and AMS mapping before sending."));
        footer->Wrap(FromDIP(245));
        layout->Add(footer, 0, wxALL, FromDIP(10));
        SetSizer(layout);
        FitInside();
        m_cards->Bind(wxEVT_LISTBOX, [this](wxCommandEvent&) {
            const int next = m_cards->GetSelection();
            if (next >= 0 && next != m_active) {
                if (!capture() || !activate(static_cast<std::size_t>(next))) redraw();
            }
        });
    }
    ~MultiPrintPanel() override { if (workspace == this) workspace = nullptr; }
    bool enabled = false;

    void run_batch(bool slice_only = false);
private:
    Plater* m_plater;
    wxListBox* m_cards = nullptr;
    wxStaticText* m_note = nullptr;
    std::vector<Target> m_targets;
    int m_active = -1;

    static nlohmann::json encode_config(const DynamicPrintConfig& config) {
        auto values = nlohmann::json::object();
        for (const auto& key : config.keys()) values[key] = config.option(key)->serialize();
        return values;
    }
    static DynamicPrintConfig decode_config(const nlohmann::json& values) {
        if (!values.is_object() || values.size() > 4096) throw std::runtime_error("Invalid settings snapshot");
        DynamicPrintConfig config;
        for (auto it = values.begin(); it != values.end(); ++it)
            config.set_deserialize_strict(it.key(), it.value().get<std::string>());
        return config;
    }
    void save_workspace() {
        if (m_targets.empty() || m_plater->is_background_process_slicing() || !capture()) return;
        wxFileDialog chooser(this, _L("Save Multi-print source project"), "", "", "3MF project (*.3mf)|*.3mf", wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
        if (chooser.ShowModal() != wxID_OK) return;
        try {
            const auto path = std::filesystem::u8path(chooser.GetPath().ToUTF8().data());
            if (m_plater->export_3mf(boost::filesystem::path(path.u8string()), SaveStrategy::Silence | SaveStrategy::Zip64) < 0)
                throw std::runtime_error("The source project could not be saved");
            std::string filename = path.u8string(), checksum;
            if (!bbl_calc_md5(filename, checksum)) throw std::runtime_error("The source project checksum could not be read");
            nlohmann::json saved = {{"schema", 1}, {"source_file", path.filename().u8string()}, {"source_checksum", checksum}, {"cards", nlohmann::json::array()}};
            for (const auto& target : m_targets) {
                if (!source_exists(target)) throw std::runtime_error("Remove cards whose source plate was deleted before saving");
                saved["cards"].push_back({{"id", target.id}, {"label", target.label}, {"model", target.model},
                    {"profile", target.profile}, {"plate", target.plate_index},
                    {"settings", encode_config(target.config)}, {"plate_settings", encode_config(target.plate_config)}});
            }
            const auto companion = std::filesystem::u8path(filename + ".multi-print.json");
            wxTempFile stream(wxString::FromUTF8(companion.u8string()));
            if (!stream.IsOpened() || !stream.Write(wxString::FromUTF8(saved.dump(2))) || !stream.Commit())
                throw std::runtime_error("Printer cards could not be saved");
            note(_L("Saved the 3MF project and its .multi-print.json printer cards. Reopen the 3MF normally, then use Load saved printer cards."));
        } catch (const std::exception& error) { note(wxString::FromUTF8(error.what())); }
    }
    void load_cards() {
        if (m_plater->is_background_process_slicing()) return;
        wxFileDialog chooser(this, _L("Load printer cards for the open source project"), "", "",
            "Multi-print cards (*.multi-print.json)|*.multi-print.json", wxFD_OPEN | wxFD_FILE_MUST_EXIST);
        if (chooser.ShowModal() != wxID_OK) return;
        try {
            const auto path = std::filesystem::u8path(chooser.GetPath().ToUTF8().data());
            if (std::filesystem::file_size(path) > 32 * 1024 * 1024) throw std::runtime_error("Printer cards file is too large");
            std::ifstream stream(path, std::ios::binary);
            const auto saved = nlohmann::json::parse(stream);
            if (saved.at("schema").get<int>() != 1 || !saved.at("cards").is_array() || saved.at("cards").size() > 64)
                throw std::runtime_error("Unsupported Multi-print workspace");
            const auto relative = std::filesystem::u8path(saved.at("source_file").get<std::string>());
            if (relative.has_parent_path() || relative.extension() != ".3mf") throw std::runtime_error("Invalid source project filename");
            const auto source = path.parent_path() / relative;
            std::string filename = source.u8string(), checksum;
            if (!bbl_calc_md5(filename, checksum) || checksum != saved.at("source_checksum").get<std::string>())
                throw std::runtime_error("The companion source project changed or is missing");
            const auto open = std::filesystem::u8path(m_plater->get_3mf_filename());
            if (open.empty() || !std::filesystem::equivalent(open, source))
                throw std::runtime_error("Open the saved companion 3MF project first, then load its printer cards");
            std::vector<Target> targets;
            auto& plates = m_plater->get_partplate_list();
            for (const auto& value : saved.at("cards")) {
                Target target;
                target.id = value.at("id").get<std::string>(); target.label = value.at("label").get<std::string>();
                target.model = value.at("model").get<std::string>(); target.profile = value.at("profile").get<std::string>();
                if (target.id.rfind("flashforge:", 0) == 0) target.provider = PrinterProvider::flashforge;
                else if (target.id.rfind("bambu:", 0) == 0) target.provider = PrinterProvider::bambu;
                else throw std::runtime_error("Unknown printer provider");
                if (target.id.back() == ':' || target.id.size() > 512) throw std::runtime_error("Invalid printer identity");
                target.plate_index = value.at("plate").get<int>();
                if (target.plate_index < 0 || target.plate_index >= plates.get_plate_count()) throw std::runtime_error("A saved source plate is missing");
                target.source = plates.get_plate(target.plate_index);
                target.config = decode_config(value.at("settings"));
                target.plate_config = decode_config(value.at("plate_settings"));
                const auto* model = target.config.option<ConfigOptionString>("printer_model");
                if (!model || canonical_printer_model(target.provider, model->value) != canonical_printer_model(target.provider, target.model))
                    throw std::runtime_error("A printer card contains a mismatched machine profile");
                if (std::any_of(targets.begin(), targets.end(), [&](const Target& old) {
                    return old.id == target.id && old.plate_index == target.plate_index;
                })) throw std::runtime_error("Duplicate printer card for the same source plate");
                targets.push_back(std::move(target));
            }
            m_targets = std::move(targets); m_active = -1;
            if (!m_targets.empty()) activate(0);
            redraw();
        } catch (const std::exception& error) { note(wxString::FromUTF8(error.what())); }
    }

    template<class F> void add_button(wxBoxSizer* layout, const wxString& label, F action) {
        auto* button = new wxButton(this, wxID_ANY, label);
        layout->Add(button, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8));
        button->Bind(wxEVT_BUTTON, [action](wxCommandEvent&) { action(); });
    }
    bool source_exists(const Target& target) const {
        auto& plates = m_plater->get_partplate_list();
        return target.plate_index >= 0 && target.plate_index < plates.get_plate_count() &&
            plates.get_plate(target.plate_index) == target.source;
    }
    bool profile_matches(const Target& target) const {
        const auto* bundle = wxGetApp().preset_bundle;
        if (!bundle) return false;
        const auto* model = bundle->printers.get_edited_preset().config.option<ConfigOptionString>("printer_model");
        return model && canonical_printer_model(target.provider, model->value) ==
            canonical_printer_model(target.provider, target.model);
    }
    bool capture() {
        if (m_active < 0) return true;
        auto& target = m_targets.at(static_cast<std::size_t>(m_active));
        if (!profile_matches(target)) {
            note(_L("The selected profile belongs to another printer. Restore this card's matching printer profile before switching cards."));
            return false;
        }
        auto* bundle = wxGetApp().preset_bundle;
        target.config = bundle->full_config_secure();
        if (source_exists(target)) target.plate_config = *target.source->config();
        target.profile = bundle->printers.get_edited_preset().name;
        return true;
    }
    void note(const wxString& value) {
        m_note->SetLabel(value); m_note->Wrap(FromDIP(245)); Layout(); FitInside();
    }
    void redraw() {
        m_cards->Clear();
        for (const auto& target : m_targets)
            m_cards->Append(wxString::Format("Plate %d", target.plate_index + 1) + " | " +
                wxString::FromUTF8(target.label) + " | " + target.status);
        if (m_active >= 0) m_cards->SetSelection(m_active);
    }
    bool activate(std::size_t index) {
        auto& target = m_targets.at(index);
        if (!source_exists(target)) { target.status = _L("Error: source plate was removed"); redraw(); return false; }
        if (m_plater->is_background_process_slicing()) {
            note(_L("Wait for slicing to finish before switching printer cards.")); return false;
        }
        try {
            auto* bundle = wxGetApp().preset_bundle;
            DynamicPrintConfig copy = target.config;
            bundle->load_config_model("Multi-print target " + std::to_string(index + 1), std::move(copy));
            wxGetApp().load_current_presets(false, false);
            if (!source_exists(target))
                throw std::runtime_error("The source plate changed while restoring the printer profile; add this card again");
            m_plater->select_plate(target.plate_index);
            *target.source->config() = target.plate_config;
            target.source->update_slice_result_valid_state(false);
            if (!profile_matches(target)) throw std::runtime_error("Printer configuration could not be restored");
            if (target.provider == PrinterProvider::bambu) {
                auto* manager = wxGetApp().getDeviceManager();
                if (manager && manager->set_selected_machine(target.id.substr(target.id.find(':') + 1)))
                    MonitorPanel::on_machine_selected(manager->get_selected_machine());
            }
            wxGetApp().app_config->set("dual_vendor_selected_printer", target.id);
            if (target.provider == PrinterProvider::flashforge)
                wxGetApp().app_config->set("flashforge_selected_printer", target.id);
            m_active = static_cast<int>(index);
            target.status = _L("Settings loaded; slice to check compatibility");
            note(wxString::FromUTF8(target.label + "\n" + target.profile) + "\n" +
                _L("Edits in the normal settings controls apply to this card."));
            redraw(); return true;
        } catch (const std::exception& error) {
            target.status = _L("Error: ") + wxString::FromUTF8(error.what());
            note(target.status); redraw(); return false;
        }
    }
    bool validate_active() {
        if (m_active < 0) return false;
        auto& target = m_targets.at(static_cast<std::size_t>(m_active));
        bool fits = false, invalid = true;
        m_plater->validate_current_plate(fits, invalid);
        target.status = !fits ? _L("Error: plate size or filament is incompatible") : invalid ?
            _L("Error: settings or material are incompatible") : _L("Ready to slice for this printer");
        return fits && !invalid && profile_matches(target);
    }
    void add_target() {
        if (m_plater->is_background_process_slicing() || !capture()) return;
        if (m_targets.size() >= 64) { note(_L("This workspace already has 64 printer cards.")); return; }
        auto& plates = m_plater->get_partplate_list();
        const int plate = plates.get_curr_plate_index();
        PartPlate* source = plates.get_curr_plate();
        if (!source || !source->has_printable_instances()) {
            note(_L("Add an object to the source plate first.")); return;
        }
        bool picked = false;
        show_printer_picker(this, flashforge_picker_snapshot(), [this] { show_vendor_accounts(this); },
            [&picked](const std::string&) { picked = true; }, [&picked] { picked = true; });
        if (!picked) return;
        if (plate < 0 || plate >= plates.get_plate_count() || plates.get_plate(plate) != source) {
            note(_L("The source plate changed while choosing a printer. Select the source plate and add its printer again.")); return;
        }
        const auto id = wxGetApp().app_config->get("dual_vendor_selected_printer");
        auto* bundle = wxGetApp().preset_bundle;
        const auto* model = bundle->printers.get_edited_preset().config.option<ConfigOptionString>("printer_model");
        if (id.empty() || !model) return;
        const bool flashforge = id.rfind("flashforge:", 0) == 0;
        std::string label = model->value;
        if (flashforge) {
            for (const auto& device : flashforge_account_snapshot().devices) if (device.id == id) label = device.name;
        } else if (auto* manager = wxGetApp().getDeviceManager()) {
            if (auto* machine = manager->get_selected_machine()) label = machine->get_dev_name();
        }
        const auto duplicate = std::find_if(m_targets.begin(), m_targets.end(), [&](const Target& value) {
            return value.id == id && value.source == source;
        });
        if (duplicate != m_targets.end()) {
            const auto index = static_cast<std::size_t>(duplicate - m_targets.begin());
            activate(index); note(_L("This printer already has a card for this source plate.")); return;
        }
        Target target;
        target.id = id; target.label = label; target.model = model->value;
        target.provider = flashforge ? PrinterProvider::flashforge : PrinterProvider::bambu;
        target.profile = bundle->printers.get_edited_preset().name;
        target.config = bundle->full_config_secure();
        target.plate_config = *source->config();
        target.plate_index = plate; target.source = source;
        m_targets.push_back(std::move(target));
        m_active = static_cast<int>(m_targets.size() - 1);
        validate_active(); redraw();
        note(_L("Use the normal filament and process controls to customize this printer. Add another printer to print the same source plate on it."));
    }
    void remove_target() {
        if (m_active < 0 || m_plater->is_background_process_slicing()) return;
        m_targets.erase(m_targets.begin() + m_active); m_active = -1;
        if (!m_targets.empty()) activate(0);
        redraw();
    }
};

void MultiPrintPanel::run_batch(bool slice_only) {
    if (m_targets.empty() || (slice_only && m_active < 0) || m_plater->is_background_process_slicing() || !capture()) {
        note(_L("Select a printer card and finish any current slice before continuing.")); return;
    }
    wxDialog dialog(wxGetApp().mainframe, wxID_ANY, slice_only ? _L("Slice selected printer") : _L("Print to all printers"));
    auto* layout = new wxBoxSizer(wxVERTICAL);
    auto* message = new wxStaticText(&dialog, wxID_ANY,
        slice_only ? _L("Checking and slicing this printer's plate. Preview will open when ready.") :
        _L("Each card will be sliced for its printer, then its cloud send dialog will open. Incompatible cards are skipped. Cancel stops the remaining cards; jobs already sent keep running."));
    message->Wrap(dialog.FromDIP(520));
    layout->Add(message, 0, wxALL, dialog.FromDIP(16));
    auto* progress = new wxStaticText(&dialog, wxID_ANY, _L("Preparing printer cards…"));
    progress->Wrap(dialog.FromDIP(520));
    layout->Add(progress, 0, wxEXPAND | wxALL, dialog.FromDIP(16));
    layout->Add(dialog.CreateButtonSizer(wxCANCEL), 0, wxEXPAND | wxALL, dialog.FromDIP(12));
    dialog.SetSizerAndFit(layout);
    wxTimer timer(&dialog);
    std::size_t index = slice_only ? static_cast<std::size_t>(m_active) : 0;
    const std::size_t end = slice_only ? index + 1 : m_targets.size();
    bool preparing = false, slicing = false, in_send_dialog = false, waiting_flashforge = false;
    auto started = std::chrono::steady_clock::now();
    dialog.Bind(wxEVT_TIMER, [&](wxTimerEvent&) {
        if (in_send_dialog) return;
        // Validation and vendor dialogs run nested event loops. Keep a timer
        // tick from re-entering this slot while one of those dialogs is open.
        struct TickGuard {
            bool& flag;
            explicit TickGuard(bool& value) : flag(value) { flag = true; }
            ~TickGuard() { flag = false; }
        } guard(in_send_dialog);
        try {
            if (index == end) { timer.Stop(); dialog.EndModal(wxID_OK); return; }
            auto& target = m_targets[index];
            if (waiting_flashforge) {
                const auto state = flashforge_account_snapshot();
                if (state.cloud_job_running) return;
                target.status = wxString::FromUTF8(state.cloud_job_message);
                waiting_flashforge = false; ++index; slicing = false; redraw(); return;
            }
            if (!slicing) {
                if (!preparing) {
                    if (!activate(index)) { ++index; redraw(); return; }
                    preparing = true; started = std::chrono::steady_clock::now();
                    target.status = _L("Applying this printer's settings…");
                    progress->SetLabel(wxString::FromUTF8(target.label) + "\n" + target.status);
                    redraw(); return;
                }
                // Preset loading schedules native configuration/bed updates.
                // Let that work finish before validation and extruder tables.
                if (m_plater->is_background_process_slicing() || m_plater->is_background_process_update_scheduled()) return;
                if (!validate_active()) { ++index; preparing = false; redraw(); return; }
                if (!slice_only && target.provider == PrinterProvider::bambu) {
                    auto* manager = wxGetApp().getDeviceManager();
                    auto* selected = manager ? manager->get_selected_machine() : nullptr;
                    if (!wxGetApp().is_user_login(BBL_CLOUD_PROVIDER) || !selected || selected->printer_agent_id != "bbl" ||
                        qualified_printer_id(PrinterProvider::bambu, selected->get_dev_id()) != target.id) {
                        target.status = _L("Error: sign in and reconnect this Bambu printer"); ++index; preparing = false; redraw(); return;
                    }
                    if (!wxGetApp().preset_bundle->use_bbl_network() && !wxGetApp().app_config->get_bool("use_printer_agents")) {
                        target.status = _L("Error: enable the Bambu cloud route for this profile; LAN fallback is disabled");
                        ++index; preparing = false; redraw(); return;
                    }
                }
                target.status = _L("Slicing for this printer…");
                progress->SetLabel(wxString::FromUTF8(target.label) + "\n" + target.status);
                SimpleEvent slice(EVT_GLTOOLBAR_SLICE_PLATE);
                m_plater->GetEventHandler()->ProcessEvent(slice);
                slicing = true; preparing = false;
                started = std::chrono::steady_clock::now(); redraw(); return;
            }
            if (std::chrono::steady_clock::now() - started < std::chrono::seconds(2)) return;
            if (m_plater->is_background_process_slicing() || m_plater->is_background_process_update_scheduled()) return;
            auto* plate = m_plater->get_partplate_list().get_curr_plate();
            if (!plate || !plate->is_slice_result_ready_for_print()) {
                target.status = _L("Error: slicing failed; review this card's settings");
            } else if (slice_only) {
                target.status = _L("Sliced — ready to preview for this printer");
            } else {
                target.status = _L("Review the printer's send confirmation"); redraw();
                bool request_started = false;
                if (target.provider == PrinterProvider::flashforge) {
                    show_flashforge_cloud_send(&dialog, target.id, true, &request_started);
                    waiting_flashforge = request_started;
                } else {
                    SimpleEvent event(EVT_GLTOOLBAR_PRINT_PLATE);
                    m_plater->GetEventHandler()->ProcessEvent(event);
                }
                // Native send dialogs own success/failure. Closing one does not
                // prove acceptance, and a canceled/uncertain job is never retried.
                target.status = target.provider == PrinterProvider::flashforge ?
                    (request_started ? _L("Sending through Flashforge Cloud…") : _L("Not sent / canceled — review this card")) :
                    _L("Send reviewed — check Device for acceptance");
            }
            if (waiting_flashforge) return;
            ++index; slicing = false; redraw();
        } catch (const std::exception& error) {
            timer.Stop(); progress->SetLabel(wxString::FromUTF8(error.what()));
        } catch (...) {
            timer.Stop();
            progress->SetLabel(_L("Batch stopped. Check Device before retrying; no job will be automatically resent."));
        }
    });
    timer.Start(350);
    dialog.ShowModal(); timer.Stop();
    note(slice_only ? _L("Slice review ended. Open Preview to inspect this printer's plate and review any errors shown on its card.") :
        _L("Batch review ended. Check each printer in Device. Cards with errors were not sent; no job is automatically retried."));
}
} // namespace

bool multi_print_enabled() { return workspace && workspace->enabled; }
void set_multi_print_enabled(Plater* plater, bool enabled) {
    if (!plater) return;
    if (!workspace && enabled) {
        workspace = new MultiPrintPanel(plater);
        plater->add_dock_pane(workspace, "multi_print", _L("Multi-print"), "left", wxSize(285, 500), [] {});
        plater->Bind(wxEVT_AUI_PANE_CLOSE, [plater](wxAuiManagerEvent& event) {
            if (workspace && event.GetPane() && event.GetPane()->window == workspace) {
                event.Veto();
                set_multi_print_enabled(plater, false);
                if (auto* frame = wxGetApp().mainframe) {
                    const auto actions = frame->available_print_actions();
                    frame->select_print_action(actions.empty() ? MainFrame::ePrintPlate : actions.front());
                }
            } else event.Skip();
        });
    }
    if (!workspace) return;
    workspace->enabled = enabled;
    plater->show_dock_pane(workspace, enabled);
}
void print_to_all_printers() { if (workspace && workspace->enabled) workspace->run_batch(); }
}}
