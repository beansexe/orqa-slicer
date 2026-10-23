#pragma once

#include "../FlashforgeAccount.hpp"
#include "../VendorAccountsDialog.hpp"
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Slic3r { namespace GUI {

enum class FlashforgeDeviceViewState {
    unavailable, signed_out, loading, refresh_failed, no_printers,
    choose_printer, selected, invalid_snapshot
};

struct FlashforgeDeviceStatusView {
    FlashforgeDeviceViewState state = FlashforgeDeviceViewState::unavailable;
    bool busy = false;
    bool can_refresh = false;
    bool has_selected_device = false;
    std::string name;
    std::string model;
    std::string reported_status;
    std::string message;
    std::optional<std::chrono::seconds> age;
};

// UI-thread value model. It owns copies only: no SDK handles, sessions, tokens,
// printer commands or inference that an account-list result is a live connection.
class FlashforgeDeviceStatusModel {
public:
    using Clock = std::chrono::steady_clock;
    bool apply_snapshot(FlashforgeAccountSnapshot snapshot, Clock::time_point now);
    bool select(const std::string& qualified_id);
    void source_unavailable();
    FlashforgeDeviceStatusView view(Clock::time_point now) const;
    const std::vector<FlashforgeDevice>& devices() const { return m_snapshot.devices; }
    const std::string& selected_id() const { return m_selected_id; }
private:
    FlashforgeAccountSnapshot m_snapshot;
    std::string m_selected_id;
    std::uint64_t m_generation = 0;
    bool m_invalid = false;
};

}} // namespace Slic3r::GUI
