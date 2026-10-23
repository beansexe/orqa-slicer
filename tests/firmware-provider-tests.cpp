#include "firmware/FirmwareProviderData.hpp"
#include "firmware/FirmwareNotices.hpp"
#include <iostream>
#include <stdexcept>

int main()
{
    using namespace Slic3r;
    const auto update = bambu_firmware_update("test-printer", R"({"print":{"upgrade_state":{"new_version_state":1,"ota_new_version_number":"01.02"}}})");
    if (!update || update->availability != FirmwareAvailability::available || update->available_version != "01.02" || update->device_id != "bambu:test-printer") throw std::runtime_error("explicit fresh update");
    if (bambu_firmware_update("test", R"({"print":{"upgrade_state":{"ota_new_version_number":"9.9"}}})")) throw std::runtime_error("version alone must not signal update");
    if (bambu_firmware_update("test", R"({"print":{"nozzle_temper":200}})")) throw std::runtime_error("delta must not renew update freshness");
    if (bambu_firmware_update("test", "invalid JSON new_version_state")) throw std::runtime_error("malformed JSON");
    const auto none = bambu_firmware_update("test", R"({"print":{"upgrade_state":{"new_version_state":2}}})");
    if (!none || none->availability != FirmwareAvailability::none) throw std::runtime_error("explicit no update");
    const auto large = bambu_firmware_update("test", R"({"print":{"upgrade_state":{"new_version_state":4294967297}}})");
    if (!large || large->availability != FirmwareAvailability::unknown) throw std::runtime_error("large integer cannot wrap to available");
    if (bambu_firmware_update("test", R"({"print":{"upgrade_state":{"new_version_state":"1"}}})")) throw std::runtime_error("string flag is not authoritative integer");
    std::cout << "Firmware provider signal checks passed.\n";
}
