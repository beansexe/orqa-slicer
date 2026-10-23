# Cloud foundation review — 2026-10-10

Reviewed `integration/printing/FlashforgeCloudPrint.*`,
`FlashforgeCloudConnection.*`, their focused mock tests, and the native printer
picker. No vendor/account/printer traffic or physical actions were performed.
The two findings below were reported to the root agent and forwarded to their
owning agents for fixes; those follow-up changes were not re-reviewed here.

## Findings reported

1. **Picker must verify the actual resulting profile.**
   `Tab::select_preset` returns `!canceled`, including when the named preset
   could not be selected. The picker previously treated a true result as proof
   of exact selection. A nested unsaved-settings prompt can process preset
   updates; a missing target can leave an older Bambu profile active while the
   picker binds the newly requested physical Bambu machine. Check the resulting
   edited preset name and its current resolved model/nozzles against a freshly
   loaded matching candidate before choosing a device or saving selection.
   Evidence: `Tab.cpp` selects by name at line 7206 but returns `!canceled`
   near line 7310; picker `select()` used only the returned boolean.

2. **A PC client ID does not identify an account login session.**
   The connection previously detected session replacement using only
   `same_client()`. `FlashforgeAccount` generates its PC client ID once per
   object, retaining it across logout/login. A new account can therefore use
   the same client ID while the connection still owns older device topics.
   Before a live bridge is enabled, capture an opaque login epoch/account
   identity on connection open and check it during callback renewal,
   preparation and publication. Normal token refresh must retain the epoch;
   login/logout must change it. Tests should replace account ownership while
   retaining the same PC client ID, rather than only changing that ID.

## Vendor teardown evidence and release constraint

The pinned vendor's `ComWanConn::freeConn()` calls `connectionStop`, signals and
drains its application worker pool, then calls `freeConnection` under an
exclusive connection lock (`ComWanConn.cpp:44-59`). This confirms lifecycle
ordering, but does not prove the proprietary SDK joins every callback thread.
The new connection waits for observed in-flight callbacks and retains its
library owner; avoiding a later callback after owner destruction still requires
the SDK's unverified quiescence contract. Existing mock teardown tests simulate
that contract and cannot establish it for the vendor binary.

The print worker distinguishes transport delivery from physical execution,
retains known job IDs after ambiguous outcomes, and avoids automatic retries
after job creation/publication. SDK response types and callback payload cleanup
match the pinned public header. Its production file inspector, fresh account
and device mapping, operator confirmation bridge and real-device verification
remain prerequisites. Native revisions may link these foundations while actual
Flashforge cloud upload/start stays disabled.
