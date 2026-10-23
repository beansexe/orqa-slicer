#pragma once

#include "FlashforgeAccount.hpp"
#include "printing/FlashforgeCloudPrint.hpp"
#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

class wxWindow;

namespace Slic3r { namespace GUI {

struct FlashforgePickerSnapshot;

// UI-thread copied values only. No credentials or mutable account access.
struct FlashforgeAccountSnapshot {
    FlashforgeAccount::State state = FlashforgeAccount::State::unavailable;
    std::vector<FlashforgeDevice> devices;
    std::string message = "Open Printer accounts to connect Flashforge.";
    std::uint64_t generation = 1;
    std::chrono::steady_clock::time_point observed_at;
    bool busy = false;
    bool refresh_failed = false;
    std::optional<FlashforgeLiveStatus> live;
    std::chrono::steady_clock::time_point live_observed_at;
    std::string cloud_job_message;
    std::string cloud_job_id;
    bool cloud_job_running = false;
};

FlashforgeAccountSnapshot flashforge_account_snapshot();
FlashforgePickerSnapshot flashforge_picker_snapshot();
bool flashforge_account_busy();
bool refresh_flashforge_devices();
bool refresh_flashforge_status(const std::string& qualified_id);
bool send_flashforge_cloud(VendorPrinting::CloudPrintRequest request, VendorPrinting::CloudFileInspector inspector);
void cancel_flashforge_cloud();
// Drain an explicit refresh on the UI thread before AppConfig/keychain teardown.
void drain_flashforge_refresh();
void show_vendor_accounts(wxWindow* parent,
    const std::function<void(const std::string&)>& on_open_flashforge_device = {});
}} // namespace Slic3r::GUI
