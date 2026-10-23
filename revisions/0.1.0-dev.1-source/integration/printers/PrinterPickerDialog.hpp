#pragma once

#include "../FlashforgeAccount.hpp"
#include <chrono>
#include <functional>
#include <string>
#include <vector>

class wxWindow;

namespace Slic3r { namespace GUI {

struct FlashforgePickerSnapshot {
    std::vector<FlashforgeDevice> devices;
    bool signed_in = false;
    std::chrono::steady_clock::time_point observed_at;
    // UI-thread check of the session owner/generation and current device identity.
    // Called after any nested unsaved-preset prompt, before saving selection.
    std::function<bool(const std::string&)> selection_is_current;
};

// Account navigation runs after the modal picker has closed.
void show_printer_picker(wxWindow* parent, const FlashforgePickerSnapshot& flashforge,
                        const std::function<void()>& open_accounts = {});

}} // namespace Slic3r::GUI
