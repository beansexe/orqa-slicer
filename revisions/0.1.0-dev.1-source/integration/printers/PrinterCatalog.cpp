#include "PrinterCatalog.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <map>
#include <string>
#include <vector>

namespace Slic3r {
namespace {

std::string ascii_lower(const std::string& value)
{
    std::string result = value;
    for (char& c : result)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return result;
}

std::string alias_key(const std::string& value)
{
    std::string result;
    for (char c : ascii_lower(value))
        if (c != ' ' && c != '-' && c != '_') result += c;
    return result;
}

bool valid_nozzles(const std::vector<double>& values)
{
    return std::all_of(values.begin(), values.end(), [](double value) {
        return std::isfinite(value) && value > 0.;
    });
}

bool same_nozzles(const std::vector<double>& left, const std::vector<double>& right)
{
    if (left.size() != right.size()) return false;
    for (std::size_t i = 0; i < left.size(); ++i)
        if (std::abs(left[i] - right[i]) > 0.000001) return false;
    return true;
}

} // namespace

std::string printer_provider_name(PrinterProvider provider)
{
    switch (provider) {
    case PrinterProvider::bambu: return "Bambu";
    case PrinterProvider::flashforge: return "Flashforge";
    }
    return {};
}

std::string qualified_printer_id(PrinterProvider provider, const std::string& native_id)
{
    if (native_id.empty()) return {};
    switch (provider) {
    case PrinterProvider::bambu: return "bambu:" + native_id;
    case PrinterProvider::flashforge: return "flashforge:" + native_id;
    }
    return {};
}

std::string canonical_printer_model(PrinterProvider provider, const std::string& model)
{
    const std::string key = alias_key(model);
    if (provider == PrinterProvider::bambu) {
        // Model IDs come from Orca's BBL machine_model JSON records.
        if (key == "n6" || key == "x2d" || key == "bambulabx2d") return "Bambu Lab X2D";
        if (key == "blp001" || key == "x1c" || key == "x1carbon" || key == "bambulabx1carbon")
            return "Bambu Lab X1 Carbon";
        if (key == "o1s" || key == "h2s" || key == "bambulabh2s") return "Bambu Lab H2S";
    } else {
        if (key == "ad5m" || key == "adventurer5m" || key == "adventure5m" ||
            key == "flashforgeadventurer5m") return "Flashforge Adventurer 5M";
    }
    // Avoid merging other printers merely because punctuation happens to match.
    return ascii_lower(model);
}

void PrinterCatalog::set_account_readiness(PrinterProvider provider, AccountReadiness readiness)
{
    m_accounts[provider] = readiness;
    for (auto it = m_printers.begin(); it != m_printers.end();) {
        if (it->second.provider != provider) { ++it; continue; }
        if (readiness == AccountReadiness::signed_out) it = m_printers.erase(it);
        else { it->second.stale = true; ++it; }
    }
}

AccountReadiness PrinterCatalog::account_readiness(PrinterProvider provider) const
{
    const auto it = m_accounts.find(provider);
    return it == m_accounts.end() ? AccountReadiness::unavailable : it->second;
}

bool PrinterCatalog::replace(PrinterProvider provider, const std::vector<CatalogPrinter>& printers,
                             Clock::time_point observed_at, std::chrono::seconds freshness)
{
    if (account_readiness(provider) != AccountReadiness::signed_in) return false;
    std::map<std::string, PrinterEntry> replacement;
    for (const CatalogPrinter& printer : printers) {
        const std::string id = qualified_printer_id(provider, printer.native_id);
        if (id.empty() || !valid_nozzles(printer.nozzle_diameters) ||
            !replacement.emplace(id, PrinterEntry{provider, id, printer, observed_at, freshness, false}).second) {
            refresh_failed(provider);
            return false;
        }
    }
    if (freshness <= std::chrono::seconds::zero()) { refresh_failed(provider); return false; }
    for (auto it = m_printers.begin(); it != m_printers.end();)
        if (it->second.provider == provider) it = m_printers.erase(it);
        else ++it;
    m_printers.insert(replacement.begin(), replacement.end());
    return true;
}

void PrinterCatalog::refresh_failed(PrinterProvider provider)
{
    for (auto& pair : m_printers)
        if (pair.second.provider == provider) pair.second.stale = true;
}

std::vector<PrinterEntry> PrinterCatalog::entries() const
{
    std::vector<PrinterEntry> result;
    result.reserve(m_printers.size());
    for (const auto& pair : m_printers) result.push_back(pair.second);
    std::sort(result.begin(), result.end(), [](const PrinterEntry& left, const PrinterEntry& right) {
        if (left.provider != right.provider) return left.provider < right.provider;
        if (left.printer.name != right.printer.name) return left.printer.name < right.printer.name;
        return left.id < right.id;
    });
    return result;
}

const PrinterEntry* PrinterCatalog::find(const std::string& qualified_id) const
{
    const auto it = m_printers.find(qualified_id);
    return it == m_printers.end() ? nullptr : &it->second;
}

ProfileChoice PrinterCatalog::choose_profile(const std::string& qualified_id,
                                             const std::vector<CatalogProfile>& profiles,
                                             const std::string& explicit_profile_id) const
{
    const PrinterEntry* entry = find(qualified_id);
    if (!entry) return {ProfileChoiceState::missing_printer, {}, {}};
    ProfileChoice result;
    const std::string model = canonical_printer_model(entry->provider, entry->printer.model);
    if (model.empty()) return result;
    // Conflicting duplicate preset identities cannot be made safe by a UI choice.
    std::map<std::string, std::size_t> profile_ids;
    for (const CatalogProfile& profile : profiles)
        if (profile.provider == entry->provider && !profile.id.empty()) ++profile_ids[profile.id];
    for (const CatalogProfile& profile : profiles) {
        if (profile.provider != entry->provider || profile.id.empty() || profile.nozzle_diameters.empty() ||
            !valid_nozzles(profile.nozzle_diameters) || canonical_printer_model(profile.provider, profile.model) != model)
            continue;
        if (profile_ids[profile.id] != 1) return {};
        if (!entry->printer.nozzle_diameters.empty() &&
            !same_nozzles(entry->printer.nozzle_diameters, profile.nozzle_diameters)) continue;
        result.candidates.push_back(profile.id);
    }
    std::sort(result.candidates.begin(), result.candidates.end());
    if (!explicit_profile_id.empty()) {
        if (std::find(result.candidates.begin(), result.candidates.end(), explicit_profile_id) != result.candidates.end()) {
            result.state = ProfileChoiceState::selected;
            result.selected_id = explicit_profile_id;
        }
    } else if (result.candidates.size() == 1) {
        result.state = ProfileChoiceState::selected;
        result.selected_id = result.candidates.front();
    } else if (result.candidates.size() > 1) result.state = ProfileChoiceState::ambiguous;
    return result;
}

SendReadiness PrinterCatalog::send_readiness(const std::string& qualified_id,
                                            const std::vector<CatalogProfile>& profiles,
                                            const std::string& explicit_profile_id,
                                            Clock::time_point now) const
{
    const PrinterEntry* entry = find(qualified_id);
    if (!entry) return SendReadiness::missing_printer;
    const AccountReadiness account = account_readiness(entry->provider);
    if (account == AccountReadiness::signed_out) return SendReadiness::signed_out;
    if (account != AccountReadiness::signed_in) return SendReadiness::provider_unavailable;
    if (entry->stale || now < entry->observed_at || now - entry->observed_at >= entry->freshness)
        return SendReadiness::stale;
    switch (entry->printer.readiness) {
    case PrinterReadiness::offline: return SendReadiness::offline;
    case PrinterReadiness::busy: return SendReadiness::busy;
    case PrinterReadiness::error: return SendReadiness::device_error;
    case PrinterReadiness::unknown: return SendReadiness::unknown_status;
    case PrinterReadiness::ready: break;
    default: return SendReadiness::unknown_status;
    }
    if (entry->printer.capabilities.wireless_print != CapabilitySupport::supported) return SendReadiness::unsupported;
    if (!entry->printer.send_route_available) return SendReadiness::route_unavailable;
    const ProfileChoice choice = choose_profile(qualified_id, profiles, explicit_profile_id);
    if (!explicit_profile_id.empty() && choice.state != ProfileChoiceState::selected) return SendReadiness::invalid_profile;
    if (choice.state == ProfileChoiceState::ambiguous) return SendReadiness::ambiguous_profile;
    if (choice.state != ProfileChoiceState::selected) return SendReadiness::missing_profile;
    return SendReadiness::ready;
}

} // namespace Slic3r
