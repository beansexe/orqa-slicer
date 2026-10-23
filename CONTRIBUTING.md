# Working on Orqa

Use a Windows checkout with Git and Visual Studio C++ Build Tools. Keep feature
work on a branch, run `./scripts/check.ps1`, and open a pull request describing
the behavior changed and the checks performed.

## Source layout

`sources.lock.json` pins the vendor baselines. `integration/native-ui.patch`
contains the native slicer edits; the C++ integration files live alongside it.
`solidworks/` holds the current add-in source. `revisions/` and `updates/` retain
historical source and release records and are not the current editing locations.

Run `./manage.ps1 Setup`, then `./scripts/setup-build-tools.ps1` and
`./manage.ps1 BuildDeps` on a fresh build machine. Run `./manage.ps1 Build` after
changes. See README.md for publication and rollback.

When editing native files in `upstream/OrcaSlicer`, save the tracked diff back to
`integration/native-ui.patch` using UTF-8 without a BOM. Copy integration helper
changes back into `integration/`. Run `./scripts/verify-source-capture.ps1`
before committing: excluded working sources must never be the only copy of a fix.

## Release checks

- Source-only checks pass on a fresh checkout and in GitHub Actions.
- The saved patch and helpers reproduce the working slicer source.
- A complete native build and portable installation succeed.
- Launching, version switching, rollback and shared settings work.
- Release notes distinguish development checks from user hardware evidence.
- Do not include settings, account sessions, printer identifiers, proprietary
  vendor networking binaries, build caches, or machine-specific shortcuts.

Keep printer and SolidWorks acceptance manual. Automated checks must not log in,
send print jobs, move printers, install add-ins, or change the selected version.
Release packages are separate from source control. Publishing a source change
does not automatically update installed applications.
