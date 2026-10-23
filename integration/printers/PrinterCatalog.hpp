#pragma once

#include <chrono>
#include <map>
#include <string>
#include <vector>

namespace Slic3r {

enum class PrinterProvider { bambu, flashforge };
enum class AccountReadiness { unavailable, signed_out, signed_in };
enum class PrinterReadiness { unknown, offline, ready, busy, error };
enum class CapabilitySupport { unknown, unsupported, supported };

struct PrinterCapabilities {
    CapabilitySupport wireless_print = CapabilitySupport::unknown;
    CapabilitySupport camera = CapabilitySupport::unknown;
    CapabilitySupport firmware_notice = CapabilitySupport::unknown;
};

// Values supplied by a provider adapter, not inferred from display text.
struct CatalogPrinter {
    std::string native_id;
    std::string name;
    std::string model;
    std::vector<double> nozzle_diameters; // Empty means not reported.
    PrinterReadiness readiness = PrinterReadiness::unknown;
    PrinterCapabilities capabilities;
    bool send_route_available = false;
};

struct PrinterEntry {
    PrinterProvider provider;
    std::string id; // Provider-qualified opaque native identity.
    CatalogPrinter printer;
    std::chrono::steady_clock::time_point observed_at;
    std::chrono::seconds freshness;
    bool stale = false;
};

// Use the exact loaded Preset::name as id and resolved config values as metadata.
// Do not include machine_model records, hidden templates or uninstantiated presets.
struct CatalogProfile {
    PrinterProvider provider;
    std::string id;
    std::string model;
    std::vector<double> nozzle_diameters;
};

enum class ProfileChoiceState { missing_printer, missing_profile, ambiguous, selected };
struct ProfileChoice {
    ProfileChoiceState state = ProfileChoiceState::missing_profile;
    std::string selected_id;
    std::vector<std::string> candidates;
};

enum class SendReadiness {
    ready, missing_printer, signed_out, provider_unavailable, stale,
    offline, busy, device_error, unknown_status, unsupported, route_unavailable,
    missing_profile, ambiguous_profile, invalid_profile
};

std::string printer_provider_name(PrinterProvider provider);
std::string qualified_printer_id(PrinterProvider provider, const std::string& native_id);
// Known aliases are provider-specific. Unknown names remain exact, case-folded names.
std::string canonical_printer_model(PrinterProvider provider, const std::string& model);

// UI-thread value model. Provider adapters copy their snapshots into it; it never
// owns native devices, sessions, credentials or a network transport.
class PrinterCatalog {
public:
    using Clock = std::chrono::steady_clock;
    void set_account_readiness(PrinterProvider provider, AccountReadiness readiness);
    AccountReadiness account_readiness(PrinterProvider provider) const;
    // A signed-in provider's complete snapshot replaces only that provider's list.
    // A malformed snapshot is rejected atomically and its previous list becomes stale.
    bool replace(PrinterProvider provider, const std::vector<CatalogPrinter>& printers,
                 Clock::time_point observed_at, std::chrono::seconds freshness);
    void refresh_failed(PrinterProvider provider);
    std::vector<PrinterEntry> entries() const;
    const PrinterEntry* find(const std::string& qualified_id) const;
    ProfileChoice choose_profile(const std::string& qualified_id,
                                 const std::vector<CatalogProfile>& profiles,
                                 const std::string& explicit_profile_id = {}) const;
    // Validate the actual edited preset after a nested native preset dialog.
    // A successful Tab return alone does not prove its requested lookup succeeded.
    bool profile_selection_is_current(const std::string& qualified_id,
                                      const std::string& requested_profile_id,
                                      const CatalogProfile& edited_profile,
                                      const std::vector<CatalogProfile>& loaded_profiles) const;
    // This preliminary gate must be followed by the native provider's existing
    // material, project, connection and print checks immediately before sending.
    SendReadiness send_readiness(const std::string& qualified_id,
                                const std::vector<CatalogProfile>& profiles,
                                const std::string& explicit_profile_id,
                                Clock::time_point now) const;
private:
    std::map<PrinterProvider, AccountReadiness> m_accounts;
    std::map<std::string, PrinterEntry> m_printers;
};

} // namespace Slic3r
