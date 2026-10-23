# Build checkpoint

Status: **published and selected**. Revision **0.1.0-dev.4** is available locally
with status **needs testing**. The final native Release build and portable
installation exited successfully. Source and dependency caches remain reusable.

This revision connects Flashforge account-owned live status and current-plate
cloud sending. Select a printer and enable Monitor selected printer through
Flashforge Cloud for device queries while the page is visible. Use Choose printer
for the exact nozzle profile, slice one material, open Preview, then use Send
current plate via cloud. Start printing after delivery is unchecked by default.
The cloud path needs no LAN address. Existing LAN setup remains available.

Completed verification:

- Optimized mocked account ownership/authorization/telemetry/cleanup tests.
- Thirty-five cloud workflow simulations and connection lifecycle/fault tests.
- Archive inspector: valid plate plus 14 malformed or mutation fixture cases.
- Pinned clang-tidy 22.1.8 with the feature on and no PCH, for ten production
  files/headers and both new C++ tests.
- Fresh pinned native patch reproduction, repeated zero-change synchronization,
  and unchanged canonical/native source hashes through the final build.
- Complete install-manifest checks before and after package copying; CLI help,
  relocated Python imports/prefix, matching binary hashes, and a changed main DLL.
- Revision chooser initialized with all four saved revisions.

No GUI or real cloud/printer test was run for this revision. Proprietary plugin
callback quiescence and actual cloud acceptance remain unverified. A transport
success does not establish physical execution. The application never retries a
print command automatically. Cancel sending does not stop an already delivered
print. Check uncertain outcomes on the printer before manually retrying.

Testing steps: docs/revisions/0.1.0-dev.4.md. Results/checks:
versions/0.1.0-dev.4/BUILD-VERIFICATION.json and USER-TEST-RESULTS files.
Bambu camera playback and firmware update announcements remain deferred.

Close the current preview before opening Launch-Slicer.cmd. It follows the
selected version and reuses 0.1.0-dev.1's data_dir without copying credentials.
Keep that folder in place. Manage-Versions.cmd provides executable rollback;
account and preset changes remain shared. Directly opening an executable
bypasses the launcher's saved settings reference.

For later revisions, use manage.ps1 Build -Jobs 8, then publish with a new version
label and SettingsVersion 0.1.0-dev.1. No fresh source ZIP or dependency download
is needed. integration/native-ui.patch contains the combined native changes;
the build script also synchronizes integration sources, including printing.
