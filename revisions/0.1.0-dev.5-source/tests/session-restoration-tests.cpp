#include "FlashforgeSessionSettings.hpp"
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
struct Config {
    std::string memory = "1", disk = "1";
    bool changed = false, fail_write = false, throw_write = false;
    int writes = 0;
    void set(const char*, const char* value)
    {
        if (memory != value) { memory = value; changed = true; }
    }
    void set_dirty() { changed = true; }
    bool dirty() const { return changed; }
    void save()
    {
        ++writes;
        if (throw_write) throw std::runtime_error("simulated configuration write exception");
        if (fail_write) return;
        disk = memory;
        changed = false;
    }
};
void check(bool condition, const char* description)
{
    if (!condition) throw std::runtime_error(description);
}
}

int main()
{
    Config config;
    check(Slic3r::persist_flashforge_restoration_disabled(config, false) && config.disk == "0",
          "a confirmed remembered sign-in can enable restoration");
    check(Slic3r::persist_flashforge_restoration_disabled(config, true) && config.disk == "1",
          "logout disables restoration durably before keychain cleanup");

    config.fail_write = true;
    const int writes = config.writes;
    check(!Slic3r::persist_flashforge_restoration_disabled(config, true) && config.writes == writes + 1,
          "an unchanged flag still requires a successful write before proceeding");
    check(!Slic3r::persist_flashforge_restoration_disabled(config, false) && config.memory == "1" && config.disk == "1",
          "a failed re-enable leaves restoration disabled in memory and on disk");
    config.fail_write = false;
    config.save();
    check(config.disk == "1", "idle retry after failed re-enable cannot revive the saved account");

    config.throw_write = true;
    check(!Slic3r::persist_flashforge_restoration_disabled(config, false) && config.memory == "1" && config.disk == "1",
          "a throwing re-enable write also fails closed");
    config.throw_write = false;
    check(Slic3r::persist_flashforge_restoration_disabled(config, false) && config.disk == "0",
          "a later confirmed new sign-in can re-enable restoration");

    config.fail_write = true;
    check(!Slic3r::persist_flashforge_restoration_disabled(config, true) && config.disk == "0",
          "logout must not proceed while disabling restoration cannot be persisted");
    std::cout << "All session restoration persistence checks passed. No accounts or credentials were accessed.\n";
}
