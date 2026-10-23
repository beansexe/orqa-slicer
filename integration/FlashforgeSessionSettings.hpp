#pragma once

#include <exception>

namespace Slic3r {

inline constexpr char flashforge_restoration_setting[] = "flashforge_account_restore_disabled";

// AppConfig::save() reports write failures by leaving dirty() true. The small
// template also allows write-failure tests without starting the GUI or keychain.
template<class Config>
bool persist_flashforge_restoration_disabled(Config& config, bool disabled)
{
    config.set(flashforge_restoration_setting, disabled ? "1" : "0");
    config.set_dirty(); // Require a confirmed write even for an unchanged flag.
    try { config.save(); } catch (const std::exception&) {}
    if (!config.dirty()) return true;
    // A failed attempt to enable restoration must remain disabled in memory,
    // including if AppConfig's normal idle handler later retries the write.
    if (!disabled) config.set(flashforge_restoration_setting, "1");
    return false;
}

} // namespace Slic3r
