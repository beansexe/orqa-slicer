#include "FirmwareNotices.hpp"
#include "../printers/PrinterCatalog.hpp"
#include <chrono>
#include <map>
#include <string>
#include <vector>

namespace Slic3r {
namespace {
bool same_update(const FirmwareUpdate& left, const FirmwareUpdate& right)
{
    return left.device_id == right.device_id && left.availability == right.availability &&
           left.current_version == right.current_version && left.available_version == right.available_version &&
           left.title == right.title && left.tips == right.tips;
}
bool valid_identity(PrinterProvider provider, const std::string& id)
{
    const auto separator = id.find(':');
    return separator != std::string::npos &&
           id == qualified_printer_id(provider, id.substr(separator + 1));
}
} // namespace

void FirmwareNotices::set_account_readiness(PrinterProvider provider, AccountReadiness readiness)
{
    const auto previous = m_accounts.find(provider);
    if (previous != m_accounts.end() && previous->second == readiness) return;
    m_accounts[provider] = readiness;
    // Re-sign-in requires a fresh provider snapshot, never a revived old one.
    for (auto it = m_observations.begin(); it != m_observations.end();)
        if (it->second.provider == provider) it = m_observations.erase(it);
        else ++it;
}

bool FirmwareNotices::replace(PrinterProvider provider, const std::vector<FirmwareUpdate>& updates,
                              Clock::time_point observed_at, std::chrono::seconds freshness)
{
    const auto account = m_accounts.find(provider);
    if (account == m_accounts.end() || account->second != AccountReadiness::signed_in) return false;
    std::map<std::string, Observation> replacement;
    for (const FirmwareUpdate& update : updates) {
        if (!valid_identity(provider, update.device_id) ||
            !replacement.emplace(update.device_id, Observation{provider, update, observed_at, freshness, false}).second) {
            refresh_failed(provider);
            return false;
        }
    }
    if (freshness <= std::chrono::seconds::zero()) { refresh_failed(provider); return false; }
    for (auto it = m_observations.begin(); it != m_observations.end();)
        if (it->second.provider == provider) it = m_observations.erase(it);
        else ++it;
    m_observations.insert(replacement.begin(), replacement.end());
    return true;
}

void FirmwareNotices::refresh_failed(PrinterProvider provider)
{
    for (auto& pair : m_observations)
        if (pair.second.provider == provider) pair.second.stale = true;
}

bool FirmwareNotices::announceable(const Observation& observation, Clock::time_point now) const
{
    const auto account = m_accounts.find(observation.provider);
    return account != m_accounts.end() && account->second == AccountReadiness::signed_in &&
           observation.update.availability == FirmwareAvailability::available && !observation.stale &&
           now >= observation.observed_at && now - observation.observed_at < observation.freshness;
}

std::vector<FirmwareNotice> FirmwareNotices::pending(Clock::time_point now) const
{
    std::vector<FirmwareNotice> result;
    for (const auto& pair : m_observations) {
        const Observation& observation = pair.second;
        if (!announceable(observation, now)) continue;
        const auto handled = m_handled.find(pair.first);
        if (handled == m_handled.end() || !same_update(handled->second, observation.update))
            result.push_back({observation.provider, observation.update});
    }
    return result;
}

bool FirmwareNotices::remember(const FirmwareNotice& notice, Clock::time_point now)
{
    const auto found = m_observations.find(notice.update.device_id);
    if (found == m_observations.end() || found->second.provider != notice.provider ||
        !announceable(found->second, now) || !same_update(found->second.update, notice.update)) return false;
    m_handled[notice.update.device_id] = notice.update;
    return true;
}

bool FirmwareNotices::mark_announced(const FirmwareNotice& notice, Clock::time_point now)
{
    return remember(notice, now);
}
bool FirmwareNotices::dismiss(const FirmwareNotice& notice, Clock::time_point now)
{
    return remember(notice, now);
}

} // namespace Slic3r
