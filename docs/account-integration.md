# Account integration

The custom Orca build adds a Printer accounts menu entry, gated by the CMake
option ORCA_DUAL_VENDOR_ACCOUNTS. The option is off in a direct upstream build
and enabled by this workspace's Build command. integration/accounts-ui.patch
contains the small upstream hook; the implementation lives in integration/
and is copied into the source tree by ApplyIntegration.

## Provider ownership

Orca's Bambu account uses BBL_CLOUD_PROVIDER explicitly. Its sign-in, sign-out,
cloud state, and printer workflows continue through GUI_App and NetworkAgent.
Orca Cloud profile synchronization is a different account and is not selected
by these Bambu controls.

Flashforge has a separate account object, dynamically loaded API, device list,
and refresh token. It never references Bambu's NetworkAgent or credential store.
Device IDs use flashforge:<cloud device id>. Login, refresh, device retrieval,
and logout run sequentially on a background worker with 15-second call timeouts.
Closing the dialog is disabled during a call; forced app shutdown joins the
worker before destroying its plugin.

The adapter uses the official FlashNetwork ABI from release 1.7.6, commit
137288fa36f005194c72dd72998ca1f9b9223274. That source explicitly requires
plugin version 3.3.6 and FLASHNETWORK9.DAT. Other ABI versions are rejected.
The plugin is loaded from the user's existing vendor installation, not copied
into this repository or a release. The application identifies itself as
OrqaSlicer. No credentials were read from the vendor application's data.

## Sessions

The refresh token and account identifier are remembered using wxSecretStore under
an independent Flashforge key scoped to Orca's data directory. Passwords are not stored, and
vendor HTTP logging is disabled. Server messages are not echoed into the UI.
The remember preference survives dialog reopenings. Local logout removes the
remembered secret before attempting cloud logout and leaves Bambu signed in.
A failed cloud logout is reported.

Sign-out and disabling remembered sign-in first persist a restoration-disabled
marker. If credential deletion later fails, that marker prevents the old token
from signing in again. A configuration write failure stops the account change.
Only a successfully saved new sign-in can re-enable restoration. An offline
restored token can still be removed through the Sign out control.

Restoration refreshes the token before requesting devices. An unauthorized
list response triggers one token refresh. Invalid refresh tokens clear the
session; transient connection failures preserve the remembered token for retry.
A failed device request clears the displayed list. Opening the account dialog
refreshes the list; there is no background device monitor in this preview.

## Verification

- The adapter compiles with MSVC x64 and the Windows SDK.
- tests/build-account-tests.cmd checks simulated login, refresh, restoration,
  independent sessions, offline logout, invalid credentials, malformed success,
  missing plugin, vendor buffer cleanup, and transparent access-token refresh.
- The installed plugin reports ABI 3.3.6 and initializes successfully with its
  own FLASHNETWORK9.DAT. The probe does not log in.
- Script syntax and repeated ApplyIntegration were checked.
- Focused remembered-session tests pass for write failures, unchanged flags,
  exceptions, idle retries, and confirmed re-enablement. The modified account
  dialog passes the pinned direct clang-tidy check.
- The complete Windows dependency build passes with the pinned Python bootstrap
  and process-scoped Git long-path setting.
- The upstream clang-tidy check passes on the menu hook. Both new implementation
  files pass direct checks with the same pinned tool and no precompiled header.
  Both new headers also pass standalone checks.
- The full GUI build and real accounts still require validation before release.
  Neither account was logged in. Simulated isolation is not an end-to-end
  Bambu/Flashforge test.

Flashforge wireless dispatch, unified profile selection, and firmware notices
are not implemented by this account change. Existing Bambu printing code is
retained; regression validation still requires the full build and printers.
Firmware notices must not install firmware automatically.
