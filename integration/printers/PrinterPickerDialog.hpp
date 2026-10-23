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

// Navigation callbacks run after the modal picker (and optional LAN setup)
// closes. Bambu's native selection is complete before on_select_bambu runs.
void show_printer_picker(wxWindow* parent, const FlashforgePickerSnapshot& flashforge,
                        const std::function<void()>& open_accounts = {},
                        const std::function<void(const std::string&)>& on_select_flashforge = {},
                        const std::function<void()>& on_select_bambu = {});

}} // namespace Slic3r::GUI
