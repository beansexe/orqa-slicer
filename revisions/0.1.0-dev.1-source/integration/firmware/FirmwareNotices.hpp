#pragma once

#include "../printers/PrinterCatalog.hpp"
#include <chrono>
#include <map>
#include <string>
#include <vector>

namespace Slic3r {

enum class FirmwareAvailability { unknown, none, available };

struct FirmwareUpdate {
    std::string device_id; // Already provider-qualified, e.g. bambu:<native id>.
    // Set only from the provider's explicit update flag. Version text never
    // changes this value; an ordinary firmware version is not update evidence.
    FirmwareAvailability availability = FirmwareAvailability::unknown;
    std::string current_version;   // Empty when not reported by the provider.
    std::string available_version; // Empty when not reported by the provider.
    std::string title;
    std::string tips;
};

struct FirmwareNotice {
    PrinterProvider provider;
    FirmwareUpdate update;
};

// UI-thread value model. It owns no transport and cannot download or install
// firmware. Provider snapshots are complete; failed refreshes invalidate them.
// Announcement/dismissal memory lasts for this object's lifetime.
class FirmwareNotices {
public:
    using Clock = std::chrono::steady_clock;
    void set_account_readiness(PrinterProvider provider, AccountReadiness readiness);
    bool replace(PrinterProvider provider, const std::vector<FirmwareUpdate>& updates,
                 Clock::time_point observed_at, std::chrono::seconds freshness);
    void refresh_failed(PrinterProvider provider);
    std::vector<FirmwareNotice> pending(Clock::time_point now) const;
    // Call only after the GUI actually presents the notice. An outdated UI
    // notice cannot acknowledge/dismiss a different update that arrived later.
    bool mark_announced(const FirmwareNotice& notice, Clock::time_point now);
    bool dismiss(const FirmwareNotice& notice, Clock::time_point now);
private:
    struct Observation {
        PrinterProvider provider;
        FirmwareUpdate update;
        Clock::time_point observed_at;
        std::chrono::seconds freshness;
        bool stale = false;
    };
    std::map<PrinterProvider, AccountReadiness> m_accounts;
    std::map<std::string, Observation> m_observations;
    std::map<std::string, FirmwareUpdate> m_handled;
    bool announceable(const Observation& observation, Clock::time_point now) const;
    bool remember(const FirmwareNotice& notice, Clock::time_point now);
};

} // namespace Slic3r
