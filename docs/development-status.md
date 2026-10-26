# Development status


> Current GitHub delivery: preview 0.1.0-dev.14 and add-in 0.1.0.16. See [the current migration and verification record](github-migration.md), [first-run guide](getting-started.md), and [live acceptance checklist](release-acceptance.md). The dated checkpoints below are historical; they do not change the current selected installation.

The user authorized continued development on October 10, 2026: finish and publish
the account milestone as a revision that needs testing, then proceed with later
features using that interface as the development baseline. Real validation remains
pending until account and printer checks are actually performed.

Current work:

- October 22, 2026: revision 12 adds the SolidWorks ribbon add-in, live preset
  bridge, temporary session settings, STEP import and background external-spool
  printing route. SolidWorks installation/export and hardware acceptance remain
  pending. AMS and multiple extruders use Load in slicer and native print review.
  See docs/solidworks-workflow.md and its revision acceptance checklist.

- Revision 0.1.0-dev.10 is published and selected as needs-testing. It bundles an
  optional isolated Bambu Community Cloud networking/camera backend and the
  Flashforge 5M nozzle-field correction. Local source/build/ABI/encryption/lifetime
  and package checks passed. Real Bambu compatibility and the nozzle correction
  need user testing. Delayed Flashforge bed reporting is unresolved. See its
  combined checklist; keep minor fixes bundled into later substantial updates.

- Revision 9 user testing confirms Flashforge pause/resume/cancel, initial layer
  count, estimated time, and switching to Bambu. Temperature reporting is missing
  initially, with bed data appearing around purge. Bambu AMS readback/light work;
  movement/filament control remain blocked. See revision 9 user observations and
  docs/flashforge-temperature-investigation.md. Bundle corrections into the next
  substantial milestone; keep revision 9 selected.


- Revision 0.1.0-dev.9 is selected as needs-testing. It adds the consolidated
  Flashforge cloud job controls and device workflow. See its combined user checklist.
- User evidence confirms Bambu cloud light control and Flashforge live completed
  status, progress/current firmware/update-row presentation. Actual available-update
  notices remain unverified. See revision 7 user-test observations.

- Delivery cadence: consolidate remaining work into three larger milestones in
  docs/product-milestones.md. The Flashforge workflow is delivered in revision 9
  pending user testing. Next: Bambu cloud integration. Bundle fixes; minimize repeated
  development checks and leave hardware validation to the user.


- Revision 0.1.0-dev.8 is selected as needs-testing. It fixes Flashforge temperature
  encoding and explains Bambu restricted cloud controls without directing users to
  LAN. It does not unlock loaded-filament editing. See its revision notes.
- User evidence confirms Bambu cloud light control and Flashforge live completed
  status, progress/current firmware/update-row presentation. Actual available-update
  notices remain unverified. See revision 7 user-test observations.

- Revision 0.1.0-dev.7 is retained as needs-testing. It adds Bambu
  fresh-message firmware notices and Flashforge account-list notices/device badge.
  Focused provider-signal, notice-model and copied-account-field checks, changed
  source checks, native build/install and package/startup checks passed. User will
  test firmware availability, presentation and printer regression. No new background
  polling or firmware installation is added. See docs/revisions/0.1.0-dev.7.md.
- October 15, 2026: user confirmed revision 6 cloud job submission reached the
  printer. Physical start/completion was not specified. Saved user evidence is in
  docs/revisions/0.1.0-dev.6-user-tests.md. Revision 6 is retained for rollback.

- Revision 0.1.0-dev.6 is retained as needs-testing with user-confirmed cloud submission. It matches the
  native exporter's catalog model ID and reports specific preparation/verification
  failures. Catalog-ID regression, valid/invalid archive fixtures and diagnostics,
  pinned changed-source checks, native build/install, package/runtime/source hashes,
  and six-revision chooser checks passed. User confirmed the previous preview
  reached confirmation but failed afterward. Real GUI export/cloud delivery still
  need validation. See docs/revisions/0.1.0-dev.6.md.

- Revision 0.1.0-dev.5 is retained as needs-testing for executable rollback. It fixes
  the user-reported single-material rejection by resolving actual positive slice
  usage and material settings, with imported metadata as a fallback. Seventeen
  optimized regression checks and pinned dialog/helper/test checks passed. Native
  Release build/install, fresh patch reproduction, source hash, CLI/Python,
  package hash, and five-revision chooser checks passed. Same-plate retest and
  real cloud delivery remain pending. See docs/revisions/0.1.0-dev.5.md.

- Cloud revision 0.1.0-dev.4 is retained as needs-testing for executable rollback. It adds
  explicit selected-device cloud polling and a current-plate send dialog, with
  account-owned routing and an archive inspector. Single-nozzle, single-material
  plates are supported. Start printing after delivery defaults off. The final
  Release build, installation, pinned checks, package/runtime and chooser checks passed.
  Optimized mocked live-account tests, 35 cloud workflow cases, connection fault
  tests, and synthetic archive fixtures passed. Real SDK/cloud operation remains
  untested. The user's primary requirement is cloud printing; same-LAN support
  is an optional fallback. Testing steps are in docs/revisions/0.1.0-dev.4.md.

- Device revision 0.1.0-dev.3: Flashforge account-status page, Open device from
  the accounts list, combined printer/nozzle picker, normal profile wizard, and
  native Flashforge LAN setup entry. Source integration is complete and focused
  checks passed. Native Release build/installation completed; it is published locally
  as needs-testing and retained for executable rollback. Published CLI, relocated Python,
  binary/source hash, and three-revision chooser checks passed. The page is explicitly
  an account-list observation, without live monitoring or cloud job submission.
  User screenshots confirm the Flashforge page with recent account status, all
  four requested machine models in the chooser, and Adventurer 5M nozzle choices.
  A follow-up confirms that committing the nozzle profile updates Prepare.
  Vendor switching and wireless operation remain unconfirmed; see
  docs/revisions/0.1.0-dev.3-user-tests.md.
- Account patch 0.1.0-dev.2: deterministic Bambu homepage status, separate
  signed-in/cloud-connected labels, and independent restored cloud connections.
  Inactive providers do not refresh DeviceManager's single live device list.
  Focused Release tests, final no-PCH pinned checks, and manager metadata/launch
  checks pass. Native Release build and installation completed; the revision is
  published locally as needs-testing and retained for executable rollback. Published CLI,
  relocated Python runtime, binary hashes, and two-revision chooser checks pass.
  It references 0.1.0-dev.1's data_dir without reading or copying credentials.
- Account revision 0.1.0-dev.1: native Release build and installation completed,
  published locally as needs-testing and retained for executable rollback.
  Remembered-session logout fixes passed their focused tests and pinned checks.
  The GUI, main DLL/executable and remaining utilities have linked successfully;
  CLI/Python runtime, first-run welcome, package hash, relocated-runtime, and
  revision chooser checks passed. On October 14, 2026, the user reported successful
  sign-in to both Bambu and Flashforge, then Flashforge restoration and an
  Adventurer 5M listing. Bambu retained a session and listed devices but failed
  to connect live. Authentication method, logout isolation, complete printer-list
  correctness, and printer operation remain unverified.
- Later work: real-cloud validation and fixes, then firmware notices and further
  Flashforge controls. Bambu video remains deferred.

Integrated selection/device modules and prepared later code:

- integration/printers: provider-qualified catalog and native wx picker.
  Optimized standalone tests and pinned clang-tidy pass. Exact profile/nozzle
  choices, vendor isolation, return to Bambu, and stale/offline gates are covered.
  Linked into 0.1.0-dev.3 with profile-wizard and native Connection setup entries.
- integration/devices: copied Flashforge account observations, native device
  page, refresh messages/age, and exact identity selection. Mock-value tests and
  pinned model/header/GUI checks pass; linked into 0.1.0-dev.3. One shared account
  worker serves the dialog and explicit refresh, with shutdown drain before
  settings teardown. GUI recreation preserves service availability.
- integration/printing: cloud upload/create/MQTT-start workflow and connection
  lifecycle. Thirty-five workflow simulations and connection fault tests pass;
  pinned source/header/test checks pass. Revision 4 links the archive inspector
  and account/operator bridges; final native/package verification passed.
  Actual cloud acceptance and proprietary callback behavior need user testing.
- integration/firmware: authoritative update-availability value model with
  per-device deduplication, dismissal, freshness, and provider isolation. Optimized
  tests and pinned checks pass. Provider/GUI bridges remain pending.

The revision chooser initializes successfully through its validation mode.
Publishing rejects incomplete portable output. Real visual and user workflow
validation remains part of revision testing.

Review notes are saved in docs/cloud-foundation-review.md. The next picker must
verify the actual selected preset after nested dialogs. The MQTT adapter must
check a distinct account-login epoch because the PC client ID survives account
changes. Both fixes passed the checks in their owning modules. Proprietary SDK
callback quiescence remains an unverified SDK precondition. Revision 4 uses the
vendor stop/free lifecycle and keeps the library owner alive through teardown;
mock lifecycle tests pass, while the real vendor behavior still needs testing.

No account sign-in, printer upload, physical print, or firmware installation has
been performed by this development session.
User restart evidence confirms Flashforge restoration and an Adventurer 5M listing.
The follow-up on revision 0.1.0-dev.2 confirms live Bambu Device status after
leaving Stealth Mode through Orca Cloud and signing out of Bambu and back in.
Video is not working and is deferred. This does not establish retention across
another restart or wireless print operation.

At 2% remaining in the previous five-hour allowance, the build was stopped and
source saved in revisions/0.1.0-dev.1-source. No Windows shutdown was issued.
The user then explicitly requested finishing the build. The included allowance
has reset, and shutdown is deferred while the account revision is completed.
The usage tool exposes five-hour and weekly included allowances separately from
purchased credits; purchased credits are zero while included usage is available.
No rate-limit reset or credit purchase was initiated by the development tools.











October 19 checkpoint: revision 10 remains selected. Bambu Community Cloud Google
sign-in failure traced to missing HTTPS trust roots in Windows OpenSSL curl.
Small component repair installed, with previous component retained. No native
rebuild or account changes. Next user check: reopen via OPEN SLICER and retry
Google sign-in; then verify persistence after restart and cloud device controls.

October 19 print checkpoint: latest component patch installed in selected
revision 10. X2D homing/video confirmed, cloud print still needs user retest
after enabling task-proof headers across regions. A remaining 403 will show a
numeric server API code where supplied. No automatic job retry added. No real
printer/account requests were made by this development step. Component/source
hashes: versions/0.1.0-dev.10/BAMBU-PRINT-AUTH-FIX.json. Rollback component:
community-bambu-before-print-auth-fix. Google HTTPS repair remains included.

October 19: revision 0.1.0-dev.11 built, published and selected, needs user testing.
Adds a visible Multi-print toggle, left printer/plate cards, independent native
printer/process/filament/plate settings, per-target validation and slices, a
sequential mixed-brand cloud batch with native send review, and saved workspace
companions. Repaired Bambu Community Cloud components and prior backend choice
are included. No real job was sent by development. See revision11 user checks.
