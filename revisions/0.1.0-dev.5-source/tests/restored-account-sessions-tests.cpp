#include "RestoredAccountSessions.hpp"
#include "slic3r/Utils/CloudProvider.hpp"

#include <cstdlib>
#include <iostream>
#include <map>
#include <ostream>
#include <set>
#include <string>
#include <vector>

namespace {

struct FakeAgent {
    std::map<std::string, bool> sessions;
    std::vector<std::string> queried;

    bool is_user_login(const std::string& provider)
    {
        queried.push_back(provider);
        const auto found = sessions.find(provider);
        // NetworkAgent returns false when the requested provider is absent.
        return found != sessions.end() && found->second;
    }
};

void check(bool condition, const char* message)
{
    if (!condition) { std::cerr << "FAILED: " << message << '\n'; std::exit(1); }
}

std::set<std::string> as_set(const std::vector<std::string>& values)
{
    return {values.begin(), values.end()};
}

} // namespace

int main()
{
    using Slic3r::BBL_CLOUD_PROVIDER;
    using Slic3r::ORCA_CLOUD_PROVIDER;
    using Slic3r::restore_account_sessions;
    const std::set<std::string> both_providers = {ORCA_CLOUD_PROVIDER, BBL_CLOUD_PROVIDER};

    FakeAgent bambu_only{{{ORCA_CLOUD_PROVIDER, false}, {BBL_CLOUD_PROVIDER, true}}, {}};
    std::vector<std::string> restored;
    restore_account_sessions(bambu_only, false, [&restored](const std::string& provider) { restored.push_back(provider); });
    check(as_set(restored) == std::set<std::string>{BBL_CLOUD_PROVIDER},
          "Bambu restores while the default Orca account is signed out");
    check(restored.size() == 1, "Bambu restoration is scheduled once");
    check(as_set(bambu_only.queried) == both_providers, "restoration checks both explicit providers");

    FakeAgent both{{{ORCA_CLOUD_PROVIDER, true}, {BBL_CLOUD_PROVIDER, true}}, {}};
    restored.clear();
    restore_account_sessions(both, false, [&restored](const std::string& provider) { restored.push_back(provider); });
    check(as_set(restored) == both_providers, "both signed-in sessions restore independently of callback order");
    check(restored.size() == 2, "each signed-in session is scheduled once");

    FakeAgent neither{{{ORCA_CLOUD_PROVIDER, false}, {BBL_CLOUD_PROVIDER, false}}, {}};
    restored.clear();
    restore_account_sessions(neither, false, [&restored](const std::string& provider) { restored.push_back(provider); });
    check(restored.empty(), "signed-out sessions never schedule restoration");

    FakeAgent stealth{{{ORCA_CLOUD_PROVIDER, true}, {BBL_CLOUD_PROVIDER, true}}, {}};
    restore_account_sessions(stealth, true, [&restored](const std::string& provider) { restored.push_back(provider); });
    check(stealth.queried.empty(), "stealth mode does not query account state");
    check(restored.empty(), "stealth mode never schedules cloud restoration");

    FakeAgent missing_bambu{{{ORCA_CLOUD_PROVIDER, true}}, {}};
    restore_account_sessions(missing_bambu, false, [&restored](const std::string& provider) { restored.push_back(provider); });
    check(as_set(restored) == std::set<std::string>{ORCA_CLOUD_PROVIDER}, "missing Bambu provider leaves Orca restoration available");

    FakeAgent missing_orca{{{BBL_CLOUD_PROVIDER, true}}, {}};
    restored.clear();
    restore_account_sessions(missing_orca, false, [&restored](const std::string& provider) { restored.push_back(provider); });
    check(as_set(restored) == std::set<std::string>{BBL_CLOUD_PROVIDER}, "missing default provider does not suppress Bambu restoration");

    FakeAgent no_providers;
    restored.clear();
    restore_account_sessions(no_providers, false, [&restored](const std::string& provider) { restored.push_back(provider); });
    check(restored.empty(), "an agent with no configured providers restores nothing");

    std::cout << "Restored account session tests passed. No network or account storage accessed.\n";
}
