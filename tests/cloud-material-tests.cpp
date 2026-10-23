#include "printing/FlashforgeCloudMaterial.hpp"
#include "printing/FlashforgeCloudPrint.hpp"
#include <cstddef>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
}
int main()
{
    using namespace Slic3r::VendorPrinting;
    const std::vector<std::string> types{"PLA", "PETG", "ABS"}, colors{"#FFFFFF", "#000000", "#FF0000"};
    auto result = resolve_cloud_material({{0, 100.}}, {}, types, colors);
    check(result.status == CloudMaterialStatus::ready && result.material.tool_id == 0 && result.material.material_name == "PLA", "fresh slice without cache must resolve");
    check(result.material.tool_color == colors[0] && result.material.slot_color == colors[0] && result.material.slot_id == 0, "resolved colors and slot");
    result = resolve_cloud_material({{0, 0.}, {2, 12.}}, {}, types, colors);
    check(result.status == CloudMaterialStatus::ready && result.material.tool_id == 2 && result.material.material_name == "ABS", "ignore unused slots and preserve sparse tool ID");
    const std::vector<MaterialMapping> imported{{1, 7, "Imported PETG", "#123456", "#654321"}};
    result = resolve_cloud_material({}, imported, types, colors);
    check(result.status == CloudMaterialStatus::ready && result.material.material_name == "Imported PETG" && result.material.tool_color == "#123456" && result.material.slot_id == 0, "imported metadata fallback");
    result = resolve_cloud_material({{1, 4.}}, imported, types, colors);
    check(result.material.material_name == "Imported PETG", "matching imported material survives live usage");
    result = resolve_cloud_material({{0, 4.}}, imported, types, colors);
    check(result.material.material_name == "PLA", "stale cache for another tool is ignored");
    check(resolve_cloud_material({{0, 3.}, {1, 4.}}, {}, types, colors).status == CloudMaterialStatus::multiple_materials, "multiple used filaments rejected");
    check(resolve_cloud_material({}, {}, types, colors).status == CloudMaterialStatus::missing_usage, "empty usage is not multiple materials");
    check(resolve_cloud_material({{0, 0.}}, imported, types, colors).status == CloudMaterialStatus::missing_usage, "zero current usage cannot revive stale cache");
    check(resolve_cloud_material({{0, -1.}}, {}, types, colors).status == CloudMaterialStatus::invalid_usage, "negative usage rejected");
    check(resolve_cloud_material({{0, std::numeric_limits<double>::quiet_NaN()}}, {}, types, colors).status == CloudMaterialStatus::invalid_usage, "NaN rejected");
    check(resolve_cloud_material({{0, std::numeric_limits<double>::infinity()}}, {}, types, colors).status == CloudMaterialStatus::invalid_usage, "infinity rejected");
    check(resolve_cloud_material({{static_cast<std::size_t>(std::numeric_limits<int>::max()) + 1, 1.}}, {}, types, colors).status == CloudMaterialStatus::invalid_usage, "tool overflow rejected");
    check(resolve_cloud_material({{5, 1.}}, {}, types, colors).status == CloudMaterialStatus::missing_settings, "missing material settings rejected");
    check(resolve_cloud_material({{0, 1.}}, {}, {""}, colors).status == CloudMaterialStatus::missing_settings, "blank material type rejected");
    check(resolve_cloud_material({}, {{-1, 0, "PLA", "", ""}}, types, colors).status == CloudMaterialStatus::invalid_usage, "invalid cached tool rejected");
    check(resolve_cloud_material({}, {{0, 0, "PLA", "", ""}, {1, 0, "PETG", "", ""}}, types, colors).status == CloudMaterialStatus::multiple_materials, "multiple imported tools rejected");
    std::cout << "17 cloud material regression checks passed; no account or printer calls.\n";
}
