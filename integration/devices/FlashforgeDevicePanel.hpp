#pragma once

#include "../VendorAccountsDialog.hpp"
#include <functional>
#include <string>

class wxWindow;

namespace Slic3r { namespace GUI {

struct FlashforgeDeviceCallbacks {
    // Must return copied values on the UI thread without starting network work,
    // yielding the event loop or destroying the panel/parent.
    std::function<FlashforgeAccountSnapshot()> read_snapshot;
    std::function<bool()> refresh;
    std::function<void()> open_accounts;
};

wxWindow* create_flashforge_device_panel(wxWindow* parent, FlashforgeDeviceCallbacks callbacks);
bool select_flashforge_device(wxWindow* panel, const std::string& qualified_id);
void open_flashforge_device(const std::string& qualified_id = {});

}} // namespace Slic3r::GUI
