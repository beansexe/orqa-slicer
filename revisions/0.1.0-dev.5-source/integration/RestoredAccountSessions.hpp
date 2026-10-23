#pragma once

#include "slic3r/Utils/CloudProvider.hpp"
#include <string>

namespace Slic3r {

// Restored cloud sessions are independent of the active printer profile.
// The callback schedules normal provider-specific restoration; this helper
// never opens a connection or changes either session.
template<class Agent, class OnRestore>
void restore_account_sessions(Agent& agent, bool stealth, OnRestore on_restore)
{
    if (stealth) return;
    const std::string* const providers[] = {&ORCA_CLOUD_PROVIDER, &BBL_CLOUD_PROVIDER};
    for (const std::string* provider : providers)
        if (agent.is_user_login(*provider)) on_restore(*provider);
}

} // namespace Slic3r
