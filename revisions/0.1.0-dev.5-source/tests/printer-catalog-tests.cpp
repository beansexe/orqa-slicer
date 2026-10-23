#include "printers/PrinterCatalog.hpp"

#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <ostream>
#include <string>
#include <vector>

using namespace Slic3r;

namespace {

void check(bool condition, const char* message)
{
    if (!condition) { std::cerr << "FAILED: " << message << '\n'; std::exit(1); }
}

CatalogPrinter ready_printer(const std::string& id, const std::string& model)
{
    CatalogPrinter printer;
    printer.native_id = id;
    printer.name = "Workshop printer";
    printer.model = model;
    printer.readiness = PrinterReadiness::ready;
    printer.capabilities.wireless_print = CapabilitySupport::supported;
    printer.send_route_available = true;
    return printer;
}

} // namespace

int main()
{
    using Clock = PrinterCatalog::Clock;
    using namespace std::chrono_literals;
    const Clock::time_point now = Clock::now();
    PrinterCatalog catalog;
    CatalogPrinter bambu = ready_printer("device:001", "BL-P001");
    CatalogPrinter flashforge = ready_printer("device:001", "AD5M");
    check(!catalog.replace(PrinterProvider::bambu, {bambu}, now, 30s), "no list before account readiness");
    catalog.set_account_readiness(PrinterProvider::bambu, AccountReadiness::signed_in);
    catalog.set_account_readiness(PrinterProvider::flashforge, AccountReadiness::signed_in);
    CatalogPrinter second = bambu;
    second.native_id = "device:002";
    check(catalog.replace(PrinterProvider::bambu, {bambu, second}, now, 30s), "Bambu snapshot accepted");
    check(catalog.replace(PrinterProvider::flashforge, {flashforge}, now, 30s), "Flashforge snapshot accepted");
    check(catalog.entries().size() == 3, "same name/model never collapses devices; vendors remain separate");
    check(catalog.find("bambu:device:001") && catalog.find("flashforge:device:001"), "opaque native ID retained");
    check(qualified_printer_id(PrinterProvider::bambu, "").empty(), "empty ID rejected");

    const std::vector<CatalogProfile> profiles = {
        {PrinterProvider::bambu, "Bambu Lab X1 Carbon 0.4 nozzle", "Bambu Lab X1 Carbon", {0.4}},
        {PrinterProvider::bambu, "Bambu Lab X1 Carbon 0.6 nozzle", "X1C", {0.6}},
        {PrinterProvider::bambu, "Bambu Lab X2D 0.4 nozzle", "X2D", {0.4}},
        {PrinterProvider::bambu, "Bambu Lab H2S 0.4 nozzle", "H2S", {0.4}},
        {PrinterProvider::flashforge, "Flashforge Adventurer 5M 0.4 Nozzle", "Flashforge Adventurer 5M", {0.4}},
        {PrinterProvider::flashforge, "Flashforge Adventurer 5M 0.6 Nozzle", "Adventurer 5M", {0.6}},
        {PrinterProvider::flashforge, "Flashforge Adventurer 5M Pro 0.4 Nozzle", "Flashforge Adventurer 5M Pro", {0.4}}
    };
    const std::string bambu_id = "bambu:device:001";
    const std::string flashforge_id = "flashforge:device:001";
    const std::string b04 = profiles[0].id;
    const std::string f04 = profiles[4].id;
    check(catalog.choose_profile(bambu_id, profiles).state == ProfileChoiceState::ambiguous,
          "unknown nozzle requires explicit profile choice");
    check(catalog.send_readiness(bambu_id, profiles, {}, now) == SendReadiness::ambiguous_profile,
          "ambiguous nozzle cannot send");
    check(catalog.choose_profile(bambu_id, profiles, b04).selected_id == b04, "exact preset choice works");
    check(catalog.send_readiness(bambu_id, profiles, b04, now) == SendReadiness::ready, "explicit valid profile ready");
    check(catalog.send_readiness(bambu_id, profiles, f04, now) == SendReadiness::invalid_profile,
          "wrong vendor profile cannot send");
    check(catalog.choose_profile(flashforge_id, profiles).candidates.size() == 2, "5M Pro never aliases 5M");
    check(catalog.choose_profile("bambu:absent", profiles).state == ProfileChoiceState::missing_printer,
          "removed printer cannot retain profile selection");
    bambu.nozzle_diameters = {0.4};
    check(catalog.replace(PrinterProvider::bambu, {bambu, second}, now, 30s), "nozzle telemetry accepted");
    check(catalog.choose_profile(bambu_id, profiles).selected_id == b04, "reported nozzle chooses sole exact preset");
    check(catalog.send_readiness(bambu_id, profiles, profiles[1].id, now) == SendReadiness::invalid_profile,
          "explicit choice cannot override reported nozzle mismatch");
    std::vector<CatalogProfile> duplicate_variants = profiles;
    duplicate_variants.push_back({PrinterProvider::bambu, "Custom X1 Carbon 0.4 nozzle", "X1 Carbon", {0.4}});
    check(catalog.choose_profile(bambu_id, duplicate_variants).state == ProfileChoiceState::ambiguous,
          "custom and system presets require explicit choice even at same nozzle");
    duplicate_variants.push_back(profiles[0]);
    check(catalog.choose_profile(bambu_id, duplicate_variants, b04).state == ProfileChoiceState::missing_profile,
          "conflicting duplicate preset IDs are rejected");
    std::vector<CatalogProfile> conflicting_nozzles = profiles;
    conflicting_nozzles.push_back({PrinterProvider::bambu, b04, "X1 Carbon", {0.6}});
    check(catalog.choose_profile(bambu_id, conflicting_nozzles, b04).state == ProfileChoiceState::missing_profile,
          "duplicate preset identity remains ambiguous across mismatched nozzle metadata");
    check(catalog.send_readiness(bambu_id, profiles, b04, now + 30s) == SendReadiness::stale,
          "expiry boundary blocks send");
    check(catalog.send_readiness(bambu_id, profiles, b04, now - 1s) == SendReadiness::stale,
          "future observation timestamp blocks send");
    catalog.refresh_failed(PrinterProvider::bambu);
    check(catalog.entries().size() == 3, "failed refresh retains visible devices");
    check(catalog.send_readiness(bambu_id, profiles, b04, now) == SendReadiness::stale,
          "failed refresh blocks stale provider");
    check(catalog.send_readiness(flashforge_id, profiles, f04, now) == SendReadiness::ready,
          "one provider failure leaves other ready");
    check(catalog.replace(PrinterProvider::bambu, {bambu}, now, 30s), "successful refresh replaces stale list");
    check(!catalog.find("bambu:device:002"), "complete snapshot removes missing printer");
    check(!catalog.replace(PrinterProvider::bambu, {bambu, bambu}, now, 30s), "duplicate device IDs reject snapshot");
    check(catalog.entries().size() == 2 && catalog.find(bambu_id)->stale, "malformed snapshot is atomic and stale");
    CatalogPrinter invalid = bambu;
    invalid.nozzle_diameters = {std::numeric_limits<double>::quiet_NaN()};
    check(!catalog.replace(PrinterProvider::bambu, {invalid}, now, 30s), "invalid nozzle cannot bypass matching");
    invalid.nozzle_diameters = {-0.4};
    check(!catalog.replace(PrinterProvider::bambu, {invalid}, now, 30s), "negative nozzle rejected");
    check(!catalog.replace(PrinterProvider::bambu, {bambu}, now, 0s), "nonpositive snapshot freshness rejected");

    const std::vector<PrinterReadiness> device_states = {
        PrinterReadiness::offline, PrinterReadiness::busy, PrinterReadiness::error, PrinterReadiness::unknown
    };
    const std::vector<SendReadiness> expected = {
        SendReadiness::offline, SendReadiness::busy, SendReadiness::device_error, SendReadiness::unknown_status
    };
    for (std::size_t i = 0; i < device_states.size(); ++i) {
        bambu.readiness = device_states[i];
        check(catalog.replace(PrinterProvider::bambu, {bambu}, now, 30s), "device state snapshot accepted");
        check(catalog.send_readiness(bambu_id, profiles, b04, now) == expected[i], "unsafe device state blocks send");
    }
    bambu.readiness = PrinterReadiness::ready;
    bambu.capabilities.wireless_print = CapabilitySupport::unknown;
    catalog.replace(PrinterProvider::bambu, {bambu}, now, 30s);
    check(catalog.send_readiness(bambu_id, profiles, b04, now) == SendReadiness::unsupported,
          "unknown support never invents wireless functionality");
    bambu.capabilities.wireless_print = CapabilitySupport::supported;
    bambu.send_route_available = false;
    catalog.replace(PrinterProvider::bambu, {bambu}, now, 30s);
    check(catalog.send_readiness(bambu_id, profiles, b04, now) == SendReadiness::route_unavailable,
          "listed printers cannot send without implemented transport");

    check(canonical_printer_model(PrinterProvider::bambu, "N6") == "Bambu Lab X2D", "X2D model ID alias");
    check(canonical_printer_model(PrinterProvider::bambu, "O1S") == "Bambu Lab H2S", "H2S model ID alias");
    check(canonical_printer_model(PrinterProvider::bambu, "X1C") == "Bambu Lab X1 Carbon", "X1C display alias");
    check(canonical_printer_model(PrinterProvider::flashforge, "adventure 5m") == "Flashforge Adventurer 5M",
          "user spelling alias");
    check(canonical_printer_model(PrinterProvider::flashforge, "N6") != "Bambu Lab X2D", "vendor-specific aliases");
    check(canonical_printer_model(PrinterProvider::bambu, "Printer-A") !=
          canonical_printer_model(PrinterProvider::bambu, "Printer A"), "unknown punctuation not fuzzy matched");
    catalog.set_account_readiness(PrinterProvider::bambu, AccountReadiness::unavailable);
    check(catalog.send_readiness(bambu_id, profiles, b04, now) == SendReadiness::provider_unavailable,
          "missing provider blocks send");
    catalog.set_account_readiness(PrinterProvider::bambu, AccountReadiness::signed_out);
    check(!catalog.find(bambu_id) && catalog.find(flashforge_id), "logout clears only its own vendor");
    check(catalog.send_readiness(bambu_id, profiles, b04, now) == SendReadiness::missing_printer,
          "logout invalidates previous printer selection");
    check(catalog.replace(PrinterProvider::flashforge, {}, now, 30s) && catalog.entries().empty(),
          "empty authoritative snapshot removes devices");

    PrinterCatalog dual_catalog;
    dual_catalog.set_account_readiness(PrinterProvider::bambu, AccountReadiness::signed_in);
    CatalogPrinter x2d = ready_printer("dual-nozzles", "N6");
    x2d.nozzle_diameters = {0.4, 0.6};
    const std::vector<CatalogProfile> dual_profiles = {
        {PrinterProvider::bambu, "Custom X2D 0.4 / 0.6", "Bambu Lab X2D", {0.4, 0.6}},
        {PrinterProvider::bambu, "Custom X2D 0.6 / 0.4", "X2D", {0.6, 0.4}},
        {PrinterProvider::bambu, "Bambu Lab X2D 0.4 nozzle", "X2D", {0.4, 0.4}},
        {PrinterProvider::bambu, "Incorrect partial X2D", "X2D", {0.4}}
    };
    check(dual_catalog.replace(PrinterProvider::bambu, {x2d}, now, 30s), "dual nozzle telemetry accepted");
    check(dual_catalog.choose_profile("bambu:dual-nozzles", dual_profiles).selected_id == dual_profiles[0].id,
          "dual extruder nozzle positions and count match exactly");
    check(dual_catalog.send_readiness("bambu:dual-nozzles", dual_profiles, dual_profiles[1].id, now) == SendReadiness::invalid_profile,
          "swapped dual nozzle positions cannot be selected as compatible");
    x2d.nozzle_diameters.clear();
    dual_catalog.replace(PrinterProvider::bambu, {x2d}, now, 30s);
    check(dual_catalog.choose_profile("bambu:dual-nozzles", dual_profiles).state == ProfileChoiceState::ambiguous,
          "incomplete dual nozzle telemetry leaves explicit user choice");
    CatalogProfile edited = dual_profiles[0];
    check(dual_catalog.profile_selection_is_current("bambu:dual-nozzles", dual_profiles[0].id, edited, dual_profiles),
          "actual edited preset and fresh loaded profile validate together");
    edited.id = dual_profiles[1].id;
    check(!dual_catalog.profile_selection_is_current("bambu:dual-nozzles", dual_profiles[0].id, edited, dual_profiles),
          "a native preset lookup that leaves the old preset selected cannot bind a printer");
    edited = dual_profiles[0];
    check(!dual_catalog.profile_selection_is_current("bambu:dual-nozzles", edited.id, edited, {dual_profiles[1]}),
          "a profile removed during a nested prompt cannot bind a printer");
    edited.model = "Bambu Lab H2S";
    check(!dual_catalog.profile_selection_is_current("bambu:dual-nozzles", dual_profiles[0].id, edited, dual_profiles),
          "changed edited printer model is rejected");
    edited = dual_profiles[0];
    edited.nozzle_diameters = {0.6, 0.4};
    check(!dual_catalog.profile_selection_is_current("bambu:dual-nozzles", dual_profiles[0].id, edited, dual_profiles),
          "changed edited nozzle positions are rejected even when device telemetry is unknown");
    edited.nozzle_diameters = {std::numeric_limits<double>::quiet_NaN(), 0.6};
    check(!dual_catalog.profile_selection_is_current("bambu:dual-nozzles", dual_profiles[0].id, edited, dual_profiles),
          "nonfinite edited nozzle metadata cannot validate");
    edited = dual_profiles[0];
    edited.provider = PrinterProvider::flashforge;
    check(!dual_catalog.profile_selection_is_current("bambu:dual-nozzles", dual_profiles[0].id, edited, dual_profiles),
          "an edited preset from another provider cannot bind the selected printer");
    std::cout << "Printer catalog tests passed. No network or printer operations performed.\n";
    return 0;
}
