# Orca Slicer consolidation workspace

This is the account integration source preview (0.1.0-dev), not yet a validated merged slicer.
The official OrcaSlicer and Orca-Flashforge sources have been downloaded once into
`upstream/`. Their exact baseline commits are recorded in `sources.lock.json`.
The account adapter compiles and passes local simulated-session tests. The
installed Flashforge 3.3.6 plugin has been initialized successfully without login.
Real accounts, wireless jobs, and firmware operations have not been exercised.
The current build checkpoint and resume instructions are in `docs/build-status.md`.

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
.\manage.ps1 Publish -Version 0.1.0-dev.1
.\manage.ps1 Use -Version 0.1.0-dev.1
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
switching. Builds are never automatically deleted. `-BuildFolder` can select a
different portable output folder. Publishing currently copies the complete build
locally; this is not a binary delta updater for distributing installers.

Each published build has an isolated `data_dir`. Account sign-ins and presets
belong to that build; switching to a new version may require signing in again.
Credential migration across builds will be implemented only after the account
storage is verified. Existing installed vendor slicers retain their own settings.

The custom Build command synchronizes the account sources and enables
`ORCA_DUAL_VENDOR_ACCOUNTS`. The upstream patch is preserved in
`integration/accounts-ui.patch` so a fresh checkout can reproduce the changes.
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
