#include "../printing/FastStartWarning.hpp"
#include "FlashforgeCloudSendDialog.hpp"
#include "../FlashforgeAccount.hpp"
#include "../VendorAccountsDialog.hpp"
#include "../printers/PrinterCatalog.hpp"
#include "../printing/FlashforgeArchiveInspector.hpp"
#include "../printing/FlashforgeCloudPrint.hpp"
#include "../printing/FlashforgeCloudMaterial.hpp"
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
#include <map>
#include <vector>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

namespace Slic3r { namespace GUI {
namespace {
wxString validation_message(VendorPrinting::CloudArchiveError error)
{
    using VendorPrinting::CloudArchiveError;
    switch (error) {
    case CloudArchiveError::model_mismatch:
        return _L("The exported plate does not match the selected printer model. Choose the correct printer profile and slice again.");
    case CloudArchiveError::nozzle_mismatch:
        return _L("The exported plate does not match the selected nozzle. Choose the installed nozzle profile and slice again.");
    case CloudArchiveError::missing_plate:
        return _L("The selected plate is missing from the exported print file. Select the plate, slice it, and try again.");
    case CloudArchiveError::missing_gcode:
    case CloudArchiveError::invalid_gcode:
        return _L("The exported plate has missing or unreadable print instructions. Slice the plate again before sending.");
    case CloudArchiveError::invalid_thumbnail:
        return _L("The exported preview image could not be read. Reopen Preview and try again.");
    case CloudArchiveError::missing_metadata:
    case CloudArchiveError::invalid_metadata:
        return _L("The exported print file has missing or invalid plate settings. Slice the plate again before sending.");
    case CloudArchiveError::changed_files:
        return _L("The temporary print files changed during verification. Try preparing the plate again.");
    case CloudArchiveError::invalid_archive:
        return _L("The exported print file is incomplete or could not be opened. Try exporting the plate again.");
    case CloudArchiveError::checksum_failed:
    case CloudArchiveError::unreadable_files:
    case CloudArchiveError::read_failed:
        return _L("The temporary print files could not be read and verified. Check available disk space and folder access, then try again.");
    case CloudArchiveError::invalid_request:
    case CloudArchiveError::none:
        return _L("The selected plate could not be verified. Select the printer and plate again before sending.");
    }
    return _L("The selected plate could not be verified.");
}

struct JobFiles {
    std::filesystem::path marker, job, thumbnail;
    ~JobFiles() {
        std::error_code error;
        for (const auto& path : {job, thumbnail, marker}) if (!path.empty()) std::filesystem::remove(path, error);
    }
};
}

void show_flashforge_cloud_send(wxWindow* parent, const std::string& id, bool start_printing, bool* request_started)
{
    if (request_started) *request_started = false;
    if (start_printing && !VendorPrinting::confirm_fast_start_for_plate(parent, PLATE_CURRENT_IDX)) return;
    wxString failure_message = _L("The selected printer or filament settings could not be read. Select the matching profiles and slice again.");
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
        // Match Plater::export_3mf, which writes the vendor catalog ID rather
        // than printer_model's human-readable name into slice_info.config.
        request.printer.model_id = bundle->printers.get_edited_preset().get_printer_type(bundle);
        if (request.printer.model_id.empty()) {
            wxMessageBox(_L("The selected printer profile has no recognized printer model. Use Choose printer to select its matching profile."), _L("Flashforge Cloud"), wxOK, parent);
            return;
        }
        request.printer.nozzle_diameter = nozzles->values[0];
        request.plate_number = plater->get_partplate_list().get_curr_plate_index();
        std::vector<VendorPrinting::MaterialMapping> cached;
        for (const auto& filament : plate->get_slice_filaments_info())
            cached.push_back({filament.id, 0, filament.type, filament.color, filament.color});
        const auto config = bundle->full_config();
        const auto* types = config.option<ConfigOptionStrings>("filament_type");
        const auto* colors = config.option<ConfigOptionStrings>("filament_colour");
        const std::map<std::size_t, double> no_usage;
        const std::vector<std::string> no_settings;
        const auto* slice = plate->get_slice_result();
        const auto material = VendorPrinting::resolve_cloud_material(
            slice ? slice->print_statistics.total_volumes_per_extruder : no_usage, cached,
            types ? types->values : no_settings, colors ? colors->values : no_settings);
        if (material.status == VendorPrinting::CloudMaterialStatus::multiple_materials) {
            wxMessageBox(_L("This cloud-print preview supports a sliced plate using one filament. Use a single-material plate."), _L("Flashforge Cloud"), wxOK, parent);
            return;
        }
        if (material.status != VendorPrinting::CloudMaterialStatus::ready) {
            wxMessageBox(_L("The sliced filament's usage or material settings could not be read. Select its filament preset and slice the plate again."), _L("Flashforge Cloud"), wxOK, parent);
            return;
        }
        request.materials.push_back(material.material);
        request.union_id = boost::uuids::to_string(boost::uuids::random_generator()());
        request.destination_name = "plate_" + std::to_string(request.plate_number + 1) + ".3mf";
        const std::string preset_name = preset.name;
        wxDialog dialog(parent, wxID_ANY, _L("Send through Flashforge Cloud"), wxDefaultPosition, wxDefaultSize,
                        wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
        auto* layout = new wxBoxSizer(wxVERTICAL);
        auto* summary = new wxStaticText(&dialog, wxID_ANY, wxString::FromUTF8(device->name + "\n" + model->value) +
            wxString::Format("\n%.2f mm nozzle | Plate %d", request.printer.nozzle_diameter, request.plate_number + 1));
        layout->Add(summary, 0, wxALL, dialog.FromDIP(12));
        auto* start = new wxCheckBox(&dialog, wxID_ANY, _L("Start printing after delivery"));
        start->SetValue(start_printing);
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
        failure_message = _L("Temporary print files could not be created. Check available disk space and folder access, then try again.");
        files->marker = std::filesystem::u8path(wxFileName::CreateTempFileName("orqa-cloud-").ToStdString(wxConvUTF8));
        if (files->marker.empty()) throw std::runtime_error("temporary file unavailable");
        files->job = files->marker; files->job += ".3mf";
        files->thumbnail = files->marker; files->thumbnail += ".png";
        const auto strategy = SaveStrategy::Silence | SaveStrategy::SkipModel | SaveStrategy::WithGcode |
                              SaveStrategy::WithSliceInfo | SaveStrategy::SkipAuxiliary;
        failure_message = _L("The sliced plate could not be exported. Select the plate, slice it, and try again.");
        if (plater->export_3mf(boost::filesystem::path(files->job.u8string()), strategy, request.plate_number) < 0)
            throw std::runtime_error("plate export failed");
        failure_message = _L("The plate's preview image could not be created or saved. Reopen Preview and try again.");
        auto image = GCodeThumbnails::compress_thumbnail(plate->thumbnail_data, GCodeThumbnailsFormat::PNG);
        if (!image || !image->data || image->size == 0) throw std::runtime_error("thumbnail export failed");
        std::ofstream thumbnail(files->thumbnail, std::ios::binary);
        thumbnail.write(static_cast<const char*>(image->data), static_cast<std::streamsize>(image->size));
        thumbnail.close();
        if (!thumbnail) throw std::runtime_error("thumbnail write failed");
        request.job_path = files->job.u8string();
        request.thumbnail_path = files->thumbnail.u8string();
        failure_message = _L("The exported print files could not be read for verification. Check folder access and try again.");
        const VendorPrinting::CloudMd5Reader hash = [](const std::string& path, std::string& digest) {
            std::string file = path;
            return bbl_calc_md5(file, digest);
        };
        const auto validation = VendorPrinting::validate_flashforge_archive(request, hash);
        if (!validation.files) {
            wxMessageBox(validation_message(validation.error), _L("Flashforge Cloud"), wxOK | wxICON_ERROR, parent);
            return;
        }
        const auto inspect = VendorPrinting::make_flashforge_archive_inspector(hash);
        auto owned_inspector = [files, inspect](const VendorPrinting::CloudPrintRequest& job) { return inspect(job); };
        const bool started = send_flashforge_cloud(std::move(request), std::move(owned_inspector));
        if (request_started) *request_started = started;
        if (!started)
            wxMessageBox(_L("The account or connection changed. No cloud request was started; refresh the printer and try again."), _L("Flashforge Cloud"), wxOK, parent);
    } catch (const std::exception&) {
        wxMessageBox(failure_message, _L("Flashforge Cloud"), wxOK | wxICON_ERROR, parent);
    }
}
}}
