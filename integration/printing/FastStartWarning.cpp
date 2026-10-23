#include "FastStartWarning.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/PartPlate.hpp"
#include <wx/msgdlg.h>
#include <limits>

namespace Slic3r { namespace VendorPrinting {
using namespace GUI;
bool confirm_fast_start_for_plate(wxWindow* parent, int plate_index) {
    auto* bundle = wxGetApp().preset_bundle;
    auto* plater = wxGetApp().plater();
    if (!bundle || !plater) return false;
    const auto& preset = bundle->printers.get_edited_preset();
    const auto* code = preset.config.option<ConfigOptionString>("machine_start_gcode");
    if (!is_fast_start(preset.name, code ? code->value : "")) return true;
    auto& plates = plater->get_partplate_list();
    if (plate_index == PLATE_CURRENT_IDX) plate_index = plates.get_curr_plate_index();
    auto* plate = plate_index >= 0 ? plates.get_plate(plate_index) : nullptr;
    double volume = 0.0;
    bool found = false;
    const auto& objects = wxGetApp().model().objects;
    for (size_t o = 0; o < objects.size(); ++o) {
        const auto* object = objects[o];
        if (!object->printable) continue;
        // Positive model bodies only; excludes support/modifier meshes. This is
        // conservative for overlapping bodies and negative-volume cutouts.
        auto mesh = object->raw_mesh();
        const double body = std::abs(double(mesh.volume())) / 1000.0;
        for (size_t i = 0; i < object->instances.size(); ++i) {
            const auto* instance = object->instances[i];
            if (!instance->printable || (plate && !plate->contain_instance(int(o), int(i)))) continue;
            if (!plate && plate_index != PLATE_ALL_IDX) continue;
            volume += body * std::abs(instance->get_matrix().linear().determinant());
            found = true;
        }
    }
    if (!found) volume = std::numeric_limits<double>::quiet_NaN();
    if (!fast_start_needs_warning(volume)) return true;
    wxString amount = std::isfinite(volume) ? wxString::Format("Estimated solid model volume: %.2f cm³ (warning at 10 cm³).", volume) :
        wxString("The solid model volume could not be determined.");
    wxMessageDialog dialog(parent,
        "Fast Start is selected: " + wxString::FromUTF8(preset.name) + "\n\n" + amount +
        "\n\nFast Start skips startup checks and material loading/purge. It assumes the correct material is already loaded."
        "\n\nFor a finished part, cancel and select the normal machine profile. Print with Fast Start anyway?",
        "Review Fast Start settings", wxYES_NO | wxNO_DEFAULT | wxICON_WARNING);
    dialog.SetYesNoLabels("Print anyway", "Cancel");
    return dialog.ShowModal() == wxID_YES;
}
}}
