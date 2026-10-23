#include "FlashforgeCloudSendDialog.hpp"
#include "../FlashforgeAccount.hpp"
#include "../VendorAccountsDialog.hpp"
#include "../printers/PrinterCatalog.hpp"
#include "../printing/FlashforgeArchiveInspector.hpp"
#include "../printing/FlashforgeCloudPrint.hpp"
#include "libslic3r/AppConfig.hpp"
#include "libslic3r/Config.hpp"
#include "libslic3r/Format/bbs_3mf.hpp"
#include "libslic3r/GCode/ThumbnailData.hpp"
#include "libslic3r/GCode/Thumbnails.hpp"
#include "libslic3r/GCode/GCodeProcessor.hpp"
#include "libslic3r/Preset.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/ProjectTask.hpp"
#include "libslic3r/Utils.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/PartPlate.hpp"
#include "slic3r/GUI/Plater.hpp"
#include <boost/filesystem/path.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/defs.h>
#include <wx/dialog.h>
#include <wx/filename.h>
#include <wx/gdicmn.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/strconv.h>
#include <wx/string.h>
#include <wx/toplevel.h>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <fstream>
#include <ios>
#include <memory>
#include <limits>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

namespace Slic3r { namespace GUI {
namespace {
struct JobFiles {
    std::filesystem::path marker, job, thumbnail;
    ~JobFiles() {
        std::error_code error;
        for (const auto& path : {job, thumbnail, marker}) if (!path.empty()) std::filesystem::remove(path, error);
    }
};
}

void show_flashforge_cloud_send(wxWindow* parent, const std::string& id)
{
    try {
        auto snapshot = flashforge_account_snapshot();
        auto* bundle = wxGetApp().preset_bundle;
        auto* plater = wxGetApp().plater();
        if (snapshot.busy || !bundle || !plater || wxGetApp().app_config->get_stealth_mode()) {
            wxMessageBox(_L("Wait for the current operation, and disable Stealth Mode before sending through Flashforge Cloud."), _L("Flashforge Cloud"), wxOK, parent);
            return;
        }
        const auto device = std::find_if(snapshot.devices.begin(), snapshot.devices.end(), [&id](const FlashforgeDevice& item) { return item.id == id; });
        if (device == snapshot.devices.end()) return;
        const auto& preset = bundle->printers.get_edited_preset();
        const auto* model = preset.config.option<ConfigOptionString>("printer_model");
        const auto* nozzles = preset.config.option<ConfigOptionFloats>("nozzle_diameter");
        if (wxGetApp().app_config->get("flashforge_selected_printer") != id || !model || !nozzles ||
            nozzles->values.size() != 1 || !std::isfinite(nozzles->values[0]) ||
            canonical_printer_model(PrinterProvider::flashforge, model->value) !=
            canonical_printer_model(PrinterProvider::flashforge, device->model)) {
            wxMessageBox(_L("Use Choose printer to select this Flashforge printer and its exact single-nozzle profile before sending."), _L("Flashforge Cloud"), wxOK, parent);
            return;
        }
        PartPlate* plate = plater->get_partplate_list().get_curr_plate();
        if (!plate || !plate->is_slice_result_ready_for_print() || !plate->thumbnail_data.is_valid()) {
            wxMessageBox(_L("Slice the current plate and open Preview before sending it through Flashforge Cloud."), _L("Flashforge Cloud"), wxOK, parent);
            return;
        }
        VendorPrinting::CloudPrintRequest request;
        request.printer.id = id;
        request.printer.model_id = model->value;
        request.printer.nozzle_diameter = nozzles->values[0];
        request.plate_number = plater->get_partplate_list().get_curr_plate_index();
        // The plate cache is populated when loading a sliced 3MF, not by normal
        // slicing. Read the current slice's usage instead of treating that empty
        // cache as a multi-material plate.
        auto filaments = plate->get_slice_filaments_info();
        if (const auto* result = plate->get_slice_result(); result && !result->print_statistics.total_volumes_per_extruder.empty()) {
            filaments.clear();
            for (const auto& used : result->print_statistics.total_volumes_per_extruder) {
                if (!std::isfinite(used.second) || used.second < 0. || used.first > static_cast<std::size_t>(std::numeric_limits<int>::max()))
                    throw std::runtime_error("invalid sliced material usage");
                if (used.second == 0.) continue;
                FilamentInfo filament;
                filament.id = static_cast<int>(used.first);
                filaments.push_back(std::move(filament));
            }
        }
        if (filaments.size() != 1 || filaments[0].id < 0) {
            wxMessageBox(_L("This cloud-print preview supports a sliced plate using one filament. Use a single-material plate."), _L("Flashforge Cloud"), wxOK, parent);
            return;
        }
        const auto config = bundle->full_config();
        const auto* types = config.option<ConfigOptionStrings>("filament_type");
        const auto* colors = config.option<ConfigOptionStrings>("filament_colour");
        const auto filament_index = static_cast<std::size_t>(filaments[0].id);
        if (filaments[0].type.empty() && types && filament_index < types->values.size()) filaments[0].type = types->values[filament_index];
        if (filaments[0].color.empty() && colors && filament_index < colors->values.size()) filaments[0].color = colors->values[filament_index];
        if (filaments[0].type.empty()) {
            wxMessageBox(_L("The sliced filament's material settings could not be read. Select its filament preset and slice the plate again."), _L("Flashforge Cloud"), wxOK, parent);
            return;
        }
        request.materials.push_back({filaments[0].id, 0, filaments[0].type, filaments[0].color, filaments[0].color});
        request.union_id = boost::uuids::to_string(boost::uuids::random_generator()());
        request.destination_name = "plate_" + std::to_string(request.plate_number + 1) + ".3mf";
        const std::string preset_name = preset.name;
        wxDialog dialog(parent, wxID_ANY, _L("Send through Flashforge Cloud"), wxDefaultPosition, wxDefaultSize,
                        wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
        auto* layout = new wxBoxSizer(wxVERTICAL);
        auto* summary = new wxStaticText(&dialog, wxID_ANY, wxString::FromUTF8(device->name + "\n" + request.printer.model_id) +
            wxString::Format("\n%.2f mm nozzle | Plate %d", request.printer.nozzle_diameter, request.plate_number + 1));
        layout->Add(summary, 0, wxALL, dialog.FromDIP(12));
        auto* start = new wxCheckBox(&dialog, wxID_ANY, _L("Start printing after delivery"));
        auto* leveling = new wxCheckBox(&dialog, wxID_ANY, _L("Bed leveling before printing"));
        auto* timelapse = new wxCheckBox(&dialog, wxID_ANY, _L("Record time-lapse"));
        for (auto* option : {start, leveling, timelapse}) layout->Add(option, 0, wxALL, dialog.FromDIP(8));
        auto* buttons = new wxBoxSizer(wxHORIZONTAL);
        buttons->Add(new wxButton(&dialog, wxID_CANCEL, _L("Cancel")), 0, wxRIGHT, dialog.FromDIP(8));
        buttons->Add(new wxButton(&dialog, wxID_OK, _L("Send via cloud")));
        layout->Add(buttons, 0, wxALIGN_RIGHT | wxALL, dialog.FromDIP(12));
        dialog.SetSizerAndFit(layout);
        wxGetApp().UpdateDlgDarkUI(&dialog);
        dialog.CentreOnParent();
        if (dialog.ShowModal() != wxID_OK) return;
        request.print_now = start->GetValue();
        request.leveling = leveling->GetValue();
        request.time_lapse = timelapse->GetValue();
        const auto current = flashforge_account_snapshot();
        if (current.busy || current.generation != snapshot.generation || bundle->printers.get_edited_preset().name != preset_name ||
            wxGetApp().app_config->get("flashforge_selected_printer") != id ||
            plater->get_partplate_list().get_curr_plate() != plate || !plate->is_slice_result_ready_for_print()) return;
        auto files = std::make_shared<JobFiles>();
        files->marker = std::filesystem::u8path(wxFileName::CreateTempFileName("orqa-cloud-").ToStdString(wxConvUTF8));
        if (files->marker.empty()) throw std::runtime_error("temporary file unavailable");
        files->job = files->marker; files->job += ".3mf";
        files->thumbnail = files->marker; files->thumbnail += ".png";
        const auto strategy = SaveStrategy::Silence | SaveStrategy::SkipModel | SaveStrategy::WithGcode |
                              SaveStrategy::WithSliceInfo | SaveStrategy::SkipAuxiliary;
        if (plater->export_3mf(boost::filesystem::path(files->job.u8string()), strategy, request.plate_number) < 0)
            throw std::runtime_error("plate export failed");
        auto image = GCodeThumbnails::compress_thumbnail(plate->thumbnail_data, GCodeThumbnailsFormat::PNG);
        if (!image || !image->data || image->size == 0) throw std::runtime_error("thumbnail export failed");
        std::ofstream thumbnail(files->thumbnail, std::ios::binary);
        thumbnail.write(static_cast<const char*>(image->data), static_cast<std::streamsize>(image->size));
        thumbnail.close();
        if (!thumbnail) throw std::runtime_error("thumbnail write failed");
        request.job_path = files->job.u8string();
        request.thumbnail_path = files->thumbnail.u8string();
        const auto inspect = VendorPrinting::make_flashforge_archive_inspector([](const std::string& path, std::string& digest) {
            std::string file = path;
            return bbl_calc_md5(file, digest);
        });
        auto owned_inspector = [files, inspect](const VendorPrinting::CloudPrintRequest& job) { return inspect(job); };
        if (!owned_inspector(request)) throw std::runtime_error("sliced plate metadata could not be verified");
        if (!send_flashforge_cloud(std::move(request), std::move(owned_inspector)))
            wxMessageBox(_L("The account or connection changed. No cloud request was started; refresh the printer and try again."), _L("Flashforge Cloud"), wxOK, parent);
    } catch (const std::exception&) {
        wxMessageBox(_L("The selected sliced plate and thumbnail could not be prepared and verified. Re-slice the plate before trying again."), _L("Flashforge Cloud"), wxOK | wxICON_ERROR, parent);
    }
}
}}
