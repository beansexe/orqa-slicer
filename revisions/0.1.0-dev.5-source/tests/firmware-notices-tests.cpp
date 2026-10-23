#include "FirmwareNotices.hpp"
#include "PrinterCatalog.hpp"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Slic3r;
namespace {
void check(bool condition, const char* description)
{
    if (!condition) throw std::runtime_error(description);
}
FirmwareUpdate update(PrinterProvider provider, const std::string& id, const std::string& version)
{
    return {qualified_printer_id(provider, id), FirmwareAvailability::available, "current", version,
            "Firmware update available", "Read the vendor's release notes"};
}
}
int main()
{
    const auto now = FirmwareNotices::Clock::now();
    const auto freshness = std::chrono::seconds(30);
    FirmwareNotices notices;
    auto bambu = update(PrinterProvider::bambu, "same-native-id", "next");
    auto flash = update(PrinterProvider::flashforge, "same-native-id", "next");
    check(!notices.replace(PrinterProvider::bambu, {bambu}, now, freshness), "unavailable provider is rejected");
    notices.set_account_readiness(PrinterProvider::bambu, AccountReadiness::signed_in);
    notices.set_account_readiness(PrinterProvider::flashforge, AccountReadiness::signed_in);
    check(notices.replace(PrinterProvider::bambu, {bambu}, now, freshness), "Bambu snapshot accepted");
    check(notices.replace(PrinterProvider::flashforge, {flash}, now, freshness), "Flashforge snapshot accepted");
    auto pending = notices.pending(now);
    check(pending.size() == 2 && pending[0].update.device_id != pending[1].update.device_id,
          "matching native identities remain distinct across providers");
    const FirmwareNotice first = pending[0];
    check(notices.mark_announced(first, now), "presented notice acknowledged");
    check(notices.pending(now).size() == 1, "presented notice does not repeat");
    check(notices.replace(PrinterProvider::bambu, {bambu}, now, freshness) && notices.pending(now).size() == 1,
          "recurring identical refresh does not interrupt again");
    const FirmwareNotice flash_notice = notices.pending(now).front();
    check(notices.dismiss(flash_notice, now) && notices.pending(now).empty(), "dismissal suppresses only that exact notice");

    bambu.available_version = "different";
    check(notices.replace(PrinterProvider::bambu, {bambu}, now, freshness) && notices.pending(now).size() == 1,
          "changed firmware notice can announce again");
    check(!notices.dismiss(first, now), "outdated UI notice cannot dismiss a new release");
    notices.refresh_failed(PrinterProvider::bambu);
    check(notices.pending(now).empty(), "failed refresh invalidates pending update information");
    check(notices.replace(PrinterProvider::bambu, {bambu}, now, freshness), "successful refresh restores eligibility");
    check(notices.pending(now + freshness).empty(), "expired snapshot never announces");
    check(notices.pending(now - std::chrono::seconds(1)).empty(), "future observations never announce early");

    notices.set_account_readiness(PrinterProvider::bambu, AccountReadiness::signed_out);
    check(notices.pending(now).empty() && !notices.replace(PrinterProvider::bambu, {bambu}, now, freshness),
          "signed-out provider cannot announce or accept updates");
    notices.set_account_readiness(PrinterProvider::bambu, AccountReadiness::signed_in);
    check(notices.pending(now).empty(), "sign-in cannot revive a snapshot from the previous session");
    check(notices.replace(PrinterProvider::bambu, {bambu}, now, freshness), "fresh post-login snapshot accepted");
    flash.available_version = "another";
    check(notices.replace(PrinterProvider::flashforge, {flash}, now, freshness), "independent changed Flashforge notice accepted");
    notices.set_account_readiness(PrinterProvider::bambu, AccountReadiness::unavailable);
    check(notices.pending(now).size() == 1 && notices.pending(now).front().provider == PrinterProvider::flashforge,
          "unavailable Bambu provider leaves Flashforge announcements independent");

    flash.availability = FirmwareAvailability::none;
    check(notices.replace(PrinterProvider::flashforge, {flash}, now, freshness) && notices.pending(now).empty(),
          "different version text is not update evidence without the provider flag");
    flash.availability = FirmwareAvailability::unknown;
    check(notices.replace(PrinterProvider::flashforge, {flash}, now, freshness) && notices.pending(now).empty(),
          "unknown availability with version metadata never announces");
    flash = update(PrinterProvider::flashforge, "version-unknown", "");
    flash.current_version.clear();
    check(notices.replace(PrinterProvider::flashforge, {flash}, now, freshness) && notices.pending(now).size() == 1,
          "authoritative availability can announce without version strings");
    check(!notices.replace(PrinterProvider::flashforge, {bambu}, now, freshness) && notices.pending(now).empty(),
          "wrong provider identity is rejected and old provider snapshot becomes stale");
    check(notices.replace(PrinterProvider::flashforge, {flash}, now, freshness), "valid snapshot recovers from bad identity");
    check(!notices.replace(PrinterProvider::flashforge, {flash, flash}, now, freshness) && notices.pending(now).empty(),
          "duplicate identity snapshot is rejected atomically");
    check(!notices.replace(PrinterProvider::flashforge, {flash}, now, std::chrono::seconds(0)),
          "unbounded or zero freshness is rejected");
    flash.device_id = "flashforge:";
    check(!notices.replace(PrinterProvider::flashforge, {flash}, now, freshness), "empty native identity is rejected");

    FirmwareNotices separate_devices;
    separate_devices.set_account_readiness(PrinterProvider::bambu, AccountReadiness::signed_in);
    auto first_device = update(PrinterProvider::bambu, "first", "same-release");
    const auto second_device = update(PrinterProvider::bambu, "second", "same-release");
    check(separate_devices.replace(PrinterProvider::bambu, {first_device, second_device}, now, freshness),
          "same release on two physical printers is accepted");
    check(separate_devices.dismiss(separate_devices.pending(now).front(), now) &&
          separate_devices.pending(now).size() == 1 && separate_devices.pending(now).front().update.device_id == second_device.device_id,
          "dismissing one printer never suppresses another printer's identical release");
    first_device.tips = "Updated vendor release guidance";
    check(separate_devices.replace(PrinterProvider::bambu, {first_device, second_device}, now, freshness) &&
          separate_devices.pending(now).size() == 2,
          "changed vendor notice guidance can reannounce even without a new version string");
    separate_devices.refresh_failed(PrinterProvider::bambu);
    check(!separate_devices.mark_announced({PrinterProvider::bambu, first_device}, now),
          "stale notice cannot be acknowledged as a current firmware update");
    std::cout << "All firmware notice checks passed. No vendor, account, printer or installation calls were made.\n";
}
