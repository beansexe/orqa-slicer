# Firmware notices

`FirmwareNotices` accepts complete provider snapshots and produces pending notices
only for a signed-in provider with fresh, explicitly available updates. Device
identities must already be qualified using `qualified_printer_id`. Version strings
are display metadata and are never compared to infer availability or a latest
firmware version. Empty version metadata is allowed when the provider's update flag
is available.

After the GUI presents a notice, call `mark_announced` with the exact returned
value. `dismiss` suppresses the same value as well. Repeated identical snapshots
do not announce again, while a changed version/title/tips/current-version value
can announce again. An old UI notice cannot dismiss a changed release. This
memory lasts for the model's lifetime; it does not yet persist across restarts.
Provider logout/unavailability clears that provider's observations. Re-sign-in
requires a fresh snapshot, and failed or malformed refreshes invalidate old data.

The model has no network, download, firmware installation, or printer-command API.
It is a tested value model awaiting a native GUI/provider bridge.

## Bambu bridge at the pinned Orca source

Copy `MachineObject::upgrade_new_version` as the availability flag,
`get_dev_id()` as native identity, `ota_new_version_number` as optional available
version, and `get_ota_version()` as optional current version. Capture the value
snapshot on the UI thread at the existing `CallAfter`/`parse_json` boundary in
[GUI_App.cpp](https://github.com/OrcaSlicer/OrcaSlicer/blob/cd021162420c0f97f41b84cd8bd9b7adaa29134f/src/slic3r/GUI/GUI_App.cpp#L2410).
Capture observation time only when the original provider response contains
`upgrade_state.new_version_state`. The parser merges delta messages and retains
older fields, so a cached/merged upgrade state or ordinary `last_update_time`
must not renew firmware-data freshness. Map a fresh state of 1 to available,
2 to none, and other/absent states to unknown.
Use `BBL_CLOUD_PROVIDER` account state and invalidate snapshots on logout or a
failed/disconnected provider refresh.

The flag is parsed from `upgrade_state.new_version_state == 1` in
[DeviceManager.cpp](https://github.com/OrcaSlicer/OrcaSlicer/blob/cd021162420c0f97f41b84cd8bd9b7adaa29134f/src/slic3r/GUI/DeviceManager.cpp#L3805).
The current GUI uses this same flag in
[UpgradePanel.cpp](https://github.com/OrcaSlicer/OrcaSlicer/blob/cd021162420c0f97f41b84cd8bd9b7adaa29134f/src/slic3r/GUI/UpgradePanel.cpp#L697).
Do not use `GetUpgrade()->HasNewVersion()` at this commit:
[DeviceManager.hpp](https://github.com/OrcaSlicer/OrcaSlicer/blob/cd021162420c0f97f41b84cd8bd9b7adaa29134f/src/slic3r/GUI/DeviceManager.hpp#L170)
states that DevUpgrade's read side is unwired and inline parsing remains
authoritative. Wiring its parser directly can duplicate upgrade side effects.

## Flashforge bridge

Copy the WAN device `devId` through `qualified_printer_id(flashforge, devId)`.
Set availability to available only when `updateInfo.status` equals exactly
`"device"`; absent/other statuses remain unknown until the provider contract
defines them. Copy optional `updateInfo.title` and `updateInfo.tips` before the
SDK device list is freed. The response does not supply a separate target version
field, so leave `available_version` empty rather than parsing prose. A current
`firmwareVersion` alone is not evidence of an update.

The official release explicitly identifies Adventurer 5M/5M Pro firmware updates
using this flag in
[MultiComMgr.cpp](https://github.com/FlashForge/Orca-Flashforge/blob/137288fa36f005194c72dd72998ca1f9b9223274/src/slic3r/GUI/FlashForge/MultiComMgr.cpp#L494).
The published `fnet_update_info_t` layout is in
[FlashNetwork.h](https://github.com/FlashForge/Orca-Flashforge/blob/137288fa36f005194c72dd72998ca1f9b9223274/src/slic3r/GUI/FlashForge/FlashNetwork.h#L372).
Refresh the model only after successful account/device retrieval, and invalidate
it on authentication loss or refresh failure. These bridges are recommendations;
they have not yet been implemented or verified against real printer accounts.
