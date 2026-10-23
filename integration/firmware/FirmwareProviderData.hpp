#pragma once
#include "FirmwareNotices.hpp"
#include "../printers/PrinterCatalog.hpp"
#include <nlohmann/json.hpp>
#include <optional>
#include <string>

namespace Slic3r {
// Only a fresh explicit provider flag is evidence. Unrelated delta messages
// must not renew the age of a previously cached firmware update.
inline std::optional<FirmwareUpdate> bambu_firmware_update(const std::string& native_id, const std::string& message)
{
    if (message.size() > 4 * 1024 * 1024 || message.find("new_version_state") == std::string::npos) return {};
    const auto root = nlohmann::json::parse(message, nullptr, false);
    if (!root.is_object() || !root.contains("print") || !root["print"].is_object()) return {};
    const auto& print = root["print"];
    if (!print.contains("upgrade_state") || !print["upgrade_state"].is_object()) return {};
    const auto& state = print["upgrade_state"];
    if (!state.contains("new_version_state") || !state["new_version_state"].is_number_integer()) return {};
    FirmwareUpdate update;
    update.device_id = qualified_printer_id(PrinterProvider::bambu, native_id);
    update.availability = state["new_version_state"] == 1 ? FirmwareAvailability::available :
        state["new_version_state"] == 2 ? FirmwareAvailability::none : FirmwareAvailability::unknown;
    if (state.contains("ota_new_version_number") && state["ota_new_version_number"].is_string())
        update.available_version = state["ota_new_version_number"].get<std::string>().substr(0, 128);
    return update;
}
}
