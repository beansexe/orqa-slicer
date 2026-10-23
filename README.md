# Orqa slicer consolidation workspace

Revision **0.1.0-dev.13** adds a **10 cm³ Fast Start print warning** to Orqa and
SolidWorks add-in **0.1.0.16**. Cancel is the default; Print anyway continues once.
Closing the add-in dialog returns focus to SolidWorks; a successful Load in slicer
still switches to Orqa.
Restart Orqa after saving the current project to activate the update. Live
CAD/printing workflows still need user testing.
Run **Install SolidWorks Add-in.cmd** once, accept the Windows administrator prompt,
then enable **Orqa Print** in SolidWorks Tools → Add-ins. Part documents gain an
**Orqa** ribbon tab with **Choose and print** and **Load in slicer**.

The selectors read connected printers and compatible materials/processes from
the running Orqa session, including **Current in Orca** for unsaved filament and
process edits. STEP exports replace the same part/configuration's temporary file.
Background Print currently supports one material/one extruder on an external
spool; use Load in slicer for AMS or multiple extruders. Keep Orca running, signed
in and optionally minimized. See [the workflow](docs/solidworks-workflow.md).

The underlying revision 11 features remain available:
Close the running slicer, then use **OPEN SLICER** or Launch-Slicer.cmd. Settings
and accounts use revision 1's shared folder. The prior Bambu backend choice is
preserved. Revision 12 remains available for rollback.

**Multi-print** is a visible toggle beside Slice/Print. It opens printer cards
on the left of Prepare/Preview and defaults Print to **Print to all printers**.
Add printers for a source plate, select a card, and use the normal process,
filament and nozzle controls to customize that printer. Source objects remain
shared; each printer gets its own validated slice. The first batch workflow
opens each vendor's send confirmation to retain native AMS review.

Save Multi-print workspace writes a standard 3MF and companion printer-card
file. Reopen the 3MF normally, then load its saved cards. Keep both files together.
See docs/revisions/0.1.0-dev.11-user-tests.md for the short user checklist.

Revision10's repaired Community Cloud sign-in and print authentication are
included. X2D homing, live video and cloud printing were confirmed by the user.
Flashforge cloud printing and pause/resume/cancel were confirmed previously.
Multi-print itself and other Bambu models still require user testing. Delayed
Flashforge bed readings remain unresolved.

Checks focus on compilation, source/packaging, relocated startup and internal
callback handling. Development did not send jobs or control printers. Sources
and dependencies are cached for incremental builds; no full ZIP download is needed.
## Everyday use

Open PowerShell here:

```powershell
.\manage.ps1 Status
.\manage.ps1 Doctor
# Download portable CMake, Perl, and the Python build bootstrap once if needed:
.\scripts\setup-build-tools.ps1
# Once only: install the C++ build prerequisites and build dependencies.
# The upstream build script supports --install-vs buildtools; this installs system software.
.\manage.ps1 BuildDeps
# Repeat after code changes; reuses sources, dependencies, and build output.
.\manage.ps1 Build
.\manage.ps1 Publish -Version 0.1.0-dev.13 -SettingsVersion 0.1.0-dev.1
.\manage.ps1 Use -Version 0.1.0-dev.13
.\manage.ps1 Run
```

After a build is published and selected, double-click `Launch-Slicer.cmd` to
open it. The launcher follows the selected version, including after a rollback.
Double-click `Manage-Versions.cmd` to choose, launch, or make a saved revision the
default. It also opens that revision's testing notes. Published revisions are
marked needs-testing until their account and printer behavior is verified.

If PowerShell blocks the script, run it in a process-scoped invocation:
`powershell -NoProfile -ExecutionPolicy Bypass -File .\manage.ps1 Status`.
No machine-wide execution policy change is needed.

`Setup` reuses existing repositories. On a new machine it downloads the locked
sources. `Fetch` downloads upstream changes incrementally without changing working
files. Updating the baseline is a deliberate development step: inspect and merge
the upstream changes, verify them, then update the lock. Never reset local edits.

`Publish` saves a complete local portable build under a version label. Use a
previous label to roll back. Nothing needs to be downloaded for local version
switching. Builds are never automatically deleted. Use a new version label for
each publication and matching revision notes. `-BuildFolder` can select a
different portable output folder matching the completed install manifest.
Publishing checks all runtime/resource files before and after copying the build
locally; this is not a binary delta updater for distributing installers.

Each published build has an isolated `data_dir` by default. To retain the current
sign-ins and presets, `Publish -SettingsVersion 0.1.0-dev.1` makes the new build
use that original revision's settings folder through `--datadir`. It does not
copy credentials or settings. Keep the source revision's data_dir in place and
close the running preview before launching another build with those settings.
Use the workspace launcher or version manager for this reference to take effect;
opening the new executable directly uses its separate settings folder. Both
revisions share future account and preset changes, including after rollback.
Existing installed vendor slicers retain their own settings.

The custom Build command synchronizes the account sources and enables
`ORCA_DUAL_VENDOR_ACCOUNTS`. The upstream patches are preserved in
`integration/native-ui.patch` so a fresh pinned checkout can reproduce the changes.
It combines the native changes for all current features. The earlier three patch
files are historical snapshots and are not applied by the current build script.
The feature defaults off when building the upstream repository directly.
Build and BuildDeps default to four parallel jobs. Pass `-Jobs 8` on a machine
with sufficient CPU and memory, or reduce that number if other work needs resources.
The pinned Python 3.12 bootstrap is selected only while building. It avoids
incompatibilities between CPython's generation scripts and newer host interpreters.
The build also enables Git long-path support for its child processes without
changing the machine's Git or Windows configuration.

The preview adds **Printer accounts** to the top menu. Its Bambu controls use
Orca's existing Bambu provider explicitly (not the Orca Cloud profile account).
Flashforge uses a separate session and shows the account's printer list. On this
Windows machine, it detects the installed Orca-Flashforge plugin. Elsewhere use
Choose plugin to select the compatible library and its FLASHNETWORK9.DAT file.
Only ABI 3.3.6 is accepted, matched to Flashforge's published 1.7.6 source.
Flashforge wireless job submission is not connected by this account preview.

Compile the focused adapter tests without the whole slicer dependency tree:
`tests\build-account-tests.cmd`. `artifacts\account-tests.exe` runs simulations;
passing a plugin path probes its version; passing plugin and server-settings
paths tests initialization only. None of those test modes attempts account login.

## First integration milestone

Keep Orca's Bambu networking flow and add Flashforge as a second provider, with
independent login, refresh, logout, device list, and persisted session state.
Signing out of one must leave the other signed in. A combined device picker must
route jobs to the selected provider and its exact machine/nozzle profile. Target
machines: Bambu X2D, X1 Carbon, H2S, and Flashforge Adventurer 5M.

See `docs/account-integration.md` for the source findings and acceptance checks.
See `docs/first-sign-in.md` for the first-run account steps after a runnable
preview has been published.

Official sources and build instructions:
- https://github.com/OrcaSlicer/OrcaSlicer
- https://github.com/FlashForge/Orca-Flashforge
- https://github.com/OrcaSlicer/OrcaSlicer/wiki/how_to_build_windows

The vendor repositories carry their own licenses. Changes derived from their
AGPL code must preserve those terms. Vendor networking binaries have separate
terms; this workspace does not redistribute them.








