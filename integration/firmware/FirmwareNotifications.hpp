#pragma once
#include <string>
#include <vector>

namespace Slic3r {
class MachineObject;
struct FlashforgeDevice;
namespace GUI {
// UI thread only; announcements use existing slicer notifications.
void observe_bambu_firmware(MachineObject& machine, const std::string& provider, const std::string& message);
void observe_flashforge_firmware(bool signed_in, bool fresh, const std::vector<FlashforgeDevice>& devices);
}}
