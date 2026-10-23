#pragma once

#include "FlashforgeCloudPrint.hpp"
#include <cmath>
#include <cstddef>
#include <limits>
#include <map>
#include <string>
#include <vector>

namespace Slic3r::VendorPrinting {

enum class CloudMaterialStatus { ready, missing_usage, multiple_materials, invalid_usage, missing_settings };
struct CloudMaterialResolution {
    CloudMaterialStatus status = CloudMaterialStatus::missing_usage;
    MaterialMapping material;
};

// Fresh slices have usage statistics but no imported-3MF filament cache. Imported
// metadata is a fallback for usage, and a source of material settings by tool ID.
inline CloudMaterialResolution resolve_cloud_material(const std::map<std::size_t, double>& usage,
    const std::vector<MaterialMapping>& cached, const std::vector<std::string>& types,
    const std::vector<std::string>& colors)
{
    std::vector<int> tools;
    if (!usage.empty()) {
        for (const auto& item : usage) {
            if (!std::isfinite(item.second) || item.second < 0. || item.first > static_cast<std::size_t>(std::numeric_limits<int>::max()))
                return {CloudMaterialStatus::invalid_usage, {}};
            if (item.second > 0.) tools.push_back(static_cast<int>(item.first));
        }
    } else {
        for (const auto& item : cached) {
            if (item.tool_id < 0) return {CloudMaterialStatus::invalid_usage, {}};
            tools.push_back(item.tool_id);
        }
    }
    if (tools.empty()) return {};
    if (tools.size() != 1) return {CloudMaterialStatus::multiple_materials, {}};
    MaterialMapping material;
    material.tool_id = tools.front();
    for (const auto& item : cached) if (item.tool_id == material.tool_id) { material = item; break; }
    const auto index = static_cast<std::size_t>(material.tool_id);
    if (material.material_name.empty() && index < types.size()) material.material_name = types[index];
    if (material.tool_color.empty() && index < colors.size()) material.tool_color = colors[index];
    material.slot_id = 0; // Single-nozzle printer without a material station.
    material.slot_color = material.tool_color;
    if (material.material_name.empty()) return {CloudMaterialStatus::missing_settings, {}};
    return {CloudMaterialStatus::ready, material};
}

} // namespace Slic3r::VendorPrinting
