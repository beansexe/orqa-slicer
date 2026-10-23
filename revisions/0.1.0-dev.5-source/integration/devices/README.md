The native Flashforge Device panel presents copied account-list observations.
It is separate from Bambu's Device/Monitor surface and owns no SDK handle,
credential, printer transport or physical control. Names, models and raw status
come from the current Flashforge account list. Temperatures, job progress,
firmware details and video are absent because this snapshot source does not
provide them. The panel explicitly says that live monitoring is not connected.

There is one shared DTO: `GUI::FlashforgeAccountSnapshot`, declared in
`VendorAccountsDialog.hpp`. The snapshot provider must run on the UI thread,
return copied values without yielding, and never start a network operation.
Its observation timestamp records a successful list refresh; reading/opening the
panel does not reset it. The service generation increases before asynchronous
account/list changes. A new generation clears selection; older snapshots,
duplicate/malformed device identities and future timestamps cannot replace the
current account's displayed printer. Logout or a provider exception clears rows.
Display-only service messages expose refresh refusals (including Stealth Mode)
separately from the printer's raw reported status. Busy operations suppress a
previous result message; invalid snapshots do not expose their message text.

Use `create_flashforge_device_panel(parent, callbacks)` for the tab. Supply
`callbacks.read_snapshot = flashforge_account_snapshot`,
`callbacks.refresh = refresh_flashforge_devices`, and a root-owned Accounts
navigation callback. The 500ms UI timer only reads snapshots. Buttons invoke
callbacks immediately. After Refresh, a weak-window check permits an immediate
snapshot read only if the panel still exists; the destructor only stops the timer.
The owner drains external refresh before destroying app configuration.
Parent-owned callback captures must stay valid for the panel's lifetime.

`select_flashforge_device(window, qualified_id)` selects only an exact current
`flashforge:<id>`. `open_flashforge_device(id)` finds the current mainframe and
named child `flashforge_device_panel`, selects the `flashforge_device` tab, and
optionally selects that identity. There is no global raw panel pointer or queued
navigation callback. Selecting a row changes only this read-only display.

`tests/build-flashforge-device-status-tests.cmd` runs optimized mock-value tests.
`tests/check-flashforge-device-status.cmd` checks the pure model/header/tests with
the pinned clang-tidy. GUI source/header checks use the existing PCH-free account
compile database with an isolated derived database, without configuring a build.
Real GUI layout and account refresh behavior need user validation.
