#include "devices/FlashforgeDeviceStatus.hpp"
#include "FlashforgeAccount.hpp"
#include "VendorAccountsDialog.hpp"
#include <chrono>
#include <exception>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>

namespace {
using namespace Slic3r;
using namespace Slic3r::GUI;
using Clock = FlashforgeDeviceStatusModel::Clock;
using State = FlashforgeDeviceViewState;

void check(bool condition, const char* reason)
{
    if (!condition) throw std::runtime_error(reason);
}

FlashforgeAccountSnapshot snapshot(Clock::time_point observed)
{
    FlashforgeAccountSnapshot value;
    value.state = FlashforgeAccount::State::signed_in;
    value.generation = 5;
    value.observed_at = observed;
    value.devices = {{"flashforge:printer-a", "Workshop", "Adventurer 5M", "ready"},
                     {"flashforge:printer-b", "Workshop", "Adventurer 5M", "printing"}};
    return value;
}
}

int main()
{
    try {
        const auto observed = Clock::time_point{} + std::chrono::seconds(100);
        const auto now = observed + std::chrono::seconds(30);
        FlashforgeDeviceStatusModel model;
        check(model.view(now).state == State::unavailable, "initial state is unavailable, not connected");
        auto data = snapshot(observed);
        check(model.apply_snapshot(data, now) && model.devices().size() == 2, "copied printer list accepted");
        check(model.view(now).state == State::choose_printer && model.view(now).can_refresh, "no implicit printer selection");
        check(model.select("flashforge:printer-b"), "qualified printer selection works");
        auto view = model.view(now);
        check(view.state == State::selected && view.has_selected_device && view.reported_status == "printing", "per-printer status uses identity, not duplicate name");
        check(view.age && *view.age == std::chrono::seconds(30), "age comes from successful list observation");
        check(view.name == "Workshop" && view.model == "Adventurer 5M", "display uses reported fields");
        data.devices[1].status = "changed outside model";
        check(model.view(now).reported_status == "printing", "model owns its copy");
        check(!model.select("bambu:printer-b") && !model.select("flashforge:missing"), "cross-provider/missing identities rejected");
        check(model.selected_id() == "flashforge:printer-b", "failed selection does not replace selected printer");

        data = snapshot(observed);
        data.devices[1].status = "unrecognized-vendor-status";
        model.apply_snapshot(data, now);
        check(model.view(now).reported_status == "unrecognized-vendor-status", "unknown status is not guessed ready/offline");
        data.refresh_failed = true;
        data.message = "Refresh unavailable in Stealth Mode.";
        model.apply_snapshot(data, now);
        check(model.view(now).state == State::refresh_failed && model.view(now).has_selected_device && model.view(now).age == view.age,
              "failed refresh is explicitly stale and never resets observation time");
        check(model.view(now).message == data.message, "service refusal is exposed for display independently of last printer status");
        data.observed_at = {};
        model.apply_snapshot(data, now);
        check(model.view(now).state == State::refresh_failed && !model.view(now).age, "initial refresh failure is not shown as perpetual loading");

        data = snapshot(observed);
        data.busy = true;
        data.message = "A stale result from the previous operation";
        model.apply_snapshot(data, now);
        check(model.view(now).state == State::loading && !model.view(now).can_refresh && !model.select("flashforge:printer-a"),
              "busy service disables refresh/selection without touching the account");
        check(model.view(now).message.empty(), "busy worker suppresses stale service result text");
        data.busy = false;
        data.generation = 6;
        model.apply_snapshot(data, now);
        check(model.selected_id().empty() && !model.view(now).has_selected_device, "new operation/account generation clears old selection");
        check(model.select("flashforge:printer-a"), "selection works in new generation");
        const auto old = snapshot(observed);
        check(!model.apply_snapshot(old, now) && model.selected_id() == "flashforge:printer-a", "older asynchronous snapshot cannot replace new owner");

        data.state = FlashforgeAccount::State::signed_out;
        model.apply_snapshot(data, now);
        check(model.devices().empty() && model.selected_id().empty() && model.view(now).state == State::signed_out && !model.view(now).can_refresh,
              "sign-out clears previous account data even if caller includes it");
        data.state = FlashforgeAccount::State::unavailable;
        model.apply_snapshot(data, now);
        check(model.devices().empty() && model.view(now).state == State::unavailable, "unavailable service shows no cached account printer");

        FlashforgeDeviceStatusModel invalid;
        data = snapshot(observed);
        data.devices.push_back(data.devices.front());
        data.message = "An unverifiable snapshot message";
        check(!invalid.apply_snapshot(data, now) && invalid.devices().empty() && invalid.view(now).state == State::invalid_snapshot,
              "duplicate device identities reject the whole ambiguous snapshot");
        check(invalid.view(now).message.empty(), "invalid snapshot does not expose its result text");
        data = snapshot(observed); data.devices[0].id = "bambu:printer-a";
        check(!invalid.apply_snapshot(data, now), "wrong provider rejected");
        data = snapshot(observed); data.devices[0].id = "flashforge:";
        check(!invalid.apply_snapshot(data, now), "empty native identity rejected");
        data = snapshot(observed); data.devices[0].id += '\n';
        check(!invalid.apply_snapshot(data, now), "identity control characters rejected");
        data = snapshot(observed); data.observed_at = now + std::chrono::seconds(1);
        check(!invalid.apply_snapshot(data, now), "future observation timestamp rejected");
        FlashforgeDeviceStatusModel zero;
        data = snapshot(observed); data.generation = 0;
        check(!zero.apply_snapshot(data, now), "signed-in state requires a nonzero generation");

        data = snapshot(observed);
        invalid.apply_snapshot(data, now);
        check(invalid.select("flashforge:printer-a"), "valid snapshot recovers after malformed data");
        data.devices.erase(data.devices.begin());
        invalid.apply_snapshot(data, now);
        check(invalid.selected_id().empty(), "removed printer cannot remain selected");
        data.devices.clear();
        invalid.apply_snapshot(data, now);
        check(invalid.view(now).state == State::no_printers, "successful empty account list is distinct from refresh failure");
        data.observed_at = {};
        invalid.apply_snapshot(data, now);
        check(invalid.view(now).state == State::loading && !invalid.view(now).age, "absent observation is never fabricated from panel-open time");
        invalid.source_unavailable();
        check(invalid.devices().empty() && invalid.selected_id().empty() && invalid.view(now).state == State::unavailable,
              "snapshot provider failure clears displayed account information");
        std::cout << "Flashforge Device snapshot status tests passed. No SDK, account, network or printer calls.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Device status test failed: " << error.what() << '\n';
        return 1;
    }
}
