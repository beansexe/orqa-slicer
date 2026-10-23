The Flashforge cloud-print worker is an integration foundation. It is not wired
to the account menu, current slicer build, or a live network connection yet.

The slicer bridge must provide an authenticated FlashNetwork 3.3.6 connection,
a fresh cloud-printer status snapshot, and a file inspector that uses the slicer's
existing ZIP reader and MD5 helper to verify the selected sliced plate, model,
nozzle and thumbnail. File signatures alone do not satisfy that inspector contract.
Run the worker only after the operator chooses the printer and confirms sending
the current plate. Never expose CloudSessionView to the GUI or persist its token.

The worker follows the vendor's upload, cloud-job registration, then MQTT start
sequence. It owns SDK response buffers, carries the selected options and material
mapping through all three steps, checks files again after upload, supports cancel,
and never retries a job registration or start publish automatically. A delivered
result confirms transport acceptance; live printer status must confirm execution.
An ambiguous result keeps any known job identifier for operator review.

Reuse Orca's existing Flashforge PrintHost for LAN discovery and upload. Do not
route Flashforge printers through Bambu's NetworkAgent or switch printer network
mode or account binding automatically.

The source of the wire sequence is the pinned official Orca-Flashforge 1.7.6:
`src/slic3r/GUI/FlashForge/WanDevSendGcodeThd.cpp` and `ComWanConn.cpp`.
Its `SendToPrinter.cpp:2175-2176` supplies `model.uuid` as `unionId` and the
zero-based `model.curr_plate_index` as `plateNo`. Archive plate filenames use
one-based suffixes. The vendor's cloud job uses the destination 3MF filename as
`thumbName` while specifying `thumbType=png`; the worker preserves that convention.
The current SDK/header is `../FlashNetwork.h`; proprietary vendor binaries are
not redistributed. `tests/build-cloud-print-tests.cmd` compiles and runs simulated
workflow cases without contacting an account, network endpoint, or printer.

`FlashforgeCloudConnection` provides the accompanying worker-owned SDK connection.
Its constructor borrows a shared initialized-library owner and a thread-safe fresh
session snapshot supplier. SDK callback threads must not call `FlashforgeAccount`
directly: keep a synchronized snapshot and refresh it on the account worker.
Opening obtains MQTT settings, waits for the SDK's connected callback, subscribes
the user/common/selected device topics and sends the vendor's login synchronization.
Reconnects require `prepare()` before a new operator action; previous print requests
are never replayed. An SDK publish has neither a timeout parameter nor a printer
acknowledgement API, so transport readiness/acceptance cannot prove execution.

For the workflow bridge, set `FlashforgeCloudApi::send` to
`FlashforgeCloudConnection::send_checked` and pass `workflow_context()` as the
session's connection. Keep the SDK's original `fnet_freeWriteMultiResult` function
for result cleanup. The connection checks that the selected device topic is in its
subscriptions, that authentication remains fresh and that no disconnect or session
renewal occurs during publishing. Any ambiguity requires operator review.

Callbacks copy status/current firmware/notification metadata into a bounded queue
and release the matching SDK payloads. `drain_events()` is for the worker/UI bridge;
parse notification JSON there and show only relevant vendor fields. The vendor's
single-PC login rule is honored: another Flashforge PC client can invalidate this
connection. This has no effect on the independently owned Bambu account.

Closing calls SDK stop, waits for in-flight callbacks and calls SDK free before
releasing the library owner. The SDK's `freeConnection` must join/quiesce its own
callback threads, as the vendor lifecycle requires. Tests simulate this contract;
the proprietary binary's teardown still needs real-device integration validation.
`tests/build-cloud-connection-tests.cmd` exercises readiness timeout, callback
errors, session expiry/replacement, disconnect during publish, explicit reconnect,
buffer ownership and teardown without making any network calls.
