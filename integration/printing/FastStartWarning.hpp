#pragma once
#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>
class wxWindow;
namespace Slic3r { namespace VendorPrinting {
constexpr double fast_start_volume_cm3 = 10.0;
inline bool is_fast_start(std::string name, std::string gcode) {
    auto lower = [](std::string& s) { std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); }); };
    lower(name); lower(gcode);
    return name.find("fast start") != std::string::npos || name.find("fast-start") != std::string::npos ||
        gcode.find("orqa fast start") != std::string::npos;
}
inline bool fast_start_needs_warning(double volume_cm3) {
    // Unknown volume must not silently bypass the check.
    return !std::isfinite(volume_cm3) || volume_cm3 <= 0.0 || volume_cm3 >= fast_start_volume_cm3;
}
bool confirm_fast_start_for_plate(wxWindow* parent, int plate_index);
}}
