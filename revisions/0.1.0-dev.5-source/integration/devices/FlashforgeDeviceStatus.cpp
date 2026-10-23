#include "FlashforgeDeviceStatus.hpp"
#include "../FlashforgeAccount.hpp"
#include "../VendorAccountsDialog.hpp"
#include <algorithm>
#include <chrono>
#include <set>
#include <string>
#include <utility>

namespace Slic3r { namespace GUI {
namespace {
bool valid_identity(const std::string& id)
{
    const std::string prefix("flashforge:");
    if (id.compare(0, prefix.size(), prefix) != 0 || id.size() <= prefix.size()) return false;
    for (unsigned char c : id) if (c < 32 || c == 127) return false;
    return true;
}
}

bool FlashforgeDeviceStatusModel::apply_snapshot(FlashforgeAccountSnapshot snapshot, Clock::time_point now)
{
    // The service generation increases before asynchronous account/list changes.
    // An old completion cannot replace a newer account's displayed values.
    if (snapshot.generation < m_generation) return false;
    if (snapshot.generation != m_generation) m_selected_id.clear();
    m_generation = snapshot.generation;
    m_invalid = false;
    if (snapshot.state != FlashforgeAccount::State::signed_in) {
        snapshot.devices.clear();
        m_selected_id.clear();
    } else {
        std::set<std::string> ids;
        if (snapshot.generation == 0 || snapshot.devices.size() > 10000 || snapshot.observed_at > now) m_invalid = true;
        for (const auto& device : snapshot.devices)
            if (!valid_identity(device.id) || !ids.insert(device.id).second) m_invalid = true;
        if (m_invalid) {
            snapshot.devices.clear();
            m_selected_id.clear();
        }
    }
    m_snapshot = std::move(snapshot);
    if (!m_selected_id.empty() && std::none_of(m_snapshot.devices.begin(), m_snapshot.devices.end(),
            [this](const FlashforgeDevice& device) { return device.id == m_selected_id; })) m_selected_id.clear();
    return !m_invalid;
}

bool FlashforgeDeviceStatusModel::select(const std::string& qualified_id)
{
    if (m_invalid || m_snapshot.busy || m_snapshot.state != FlashforgeAccount::State::signed_in || !valid_identity(qualified_id)) return false;
    const auto found = std::find_if(m_snapshot.devices.begin(), m_snapshot.devices.end(),
        [&qualified_id](const FlashforgeDevice& device) { return device.id == qualified_id; });
    if (found == m_snapshot.devices.end()) return false;
    m_selected_id = qualified_id;
    return true;
}

void FlashforgeDeviceStatusModel::source_unavailable()
{
    m_snapshot = {};
    m_snapshot.generation = m_generation;
    m_selected_id.clear();
    m_invalid = false;
}

FlashforgeDeviceStatusView FlashforgeDeviceStatusModel::view(Clock::time_point now) const
{
    FlashforgeDeviceStatusView result;
    result.busy = m_snapshot.busy;
    result.can_refresh = !m_invalid && !m_snapshot.busy && m_snapshot.state == FlashforgeAccount::State::signed_in;
    if (m_invalid) { result.state = FlashforgeDeviceViewState::invalid_snapshot; return result; }
    // The service may retain the previous operation's message while a worker is
    // active. Its current busy state must not display that stale result as new.
    if (!m_snapshot.busy) result.message = m_snapshot.message;
    if (m_snapshot.state == FlashforgeAccount::State::unavailable) return result;
    if (m_snapshot.state == FlashforgeAccount::State::signed_out) {
        result.state = FlashforgeDeviceViewState::signed_out;
        return result;
    }
    if (m_snapshot.observed_at != Clock::time_point{} && now >= m_snapshot.observed_at)
        result.age = std::chrono::duration_cast<std::chrono::seconds>(now - m_snapshot.observed_at);
    const auto found = std::find_if(m_snapshot.devices.begin(), m_snapshot.devices.end(),
        [this](const FlashforgeDevice& device) { return device.id == m_selected_id; });
    if (found != m_snapshot.devices.end()) {
        result.has_selected_device = true;
        result.name = found->name;
        result.model = found->model;
        result.reported_status = found->status;
    }
    if (m_snapshot.busy) result.state = FlashforgeDeviceViewState::loading;
    else if (m_snapshot.refresh_failed) result.state = FlashforgeDeviceViewState::refresh_failed;
    else if (!result.age) result.state = FlashforgeDeviceViewState::loading;
    else if (m_snapshot.devices.empty()) result.state = FlashforgeDeviceViewState::no_printers;
    else result.state = result.has_selected_device ? FlashforgeDeviceViewState::selected : FlashforgeDeviceViewState::choose_printer;
    return result;
}

}} // namespace Slic3r::GUI
