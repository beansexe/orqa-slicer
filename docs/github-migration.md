# GitHub migration and release readiness

Repository: https://github.com/beansexe/orqa-slicer (private).

## Migration evidence

- The local workspace originally had no commits or remote.
- Initial source commit `fca5da2567d17dc7bbd7c7b5327a20d0bd754f84` was
  uploaded to `main`. The local branch tracks `origin/main`.
- A fresh clone downloaded from GitHub passed scripts/check.ps1 without upstream
  checkouts, vendor binaries, dependency caches, or account settings.
- GitHub Actions [run 1](https://github.com/beansexe/orqa-slicer/actions/runs/40032450755)
  completed successfully on Windows for the initial source commit.
- The release-polish branch also passed GitHub Actions on commit
  `85a1098fe816ccf456f9bdf0c94c2972ad36af2e`
  ([run 5](https://github.com/beansexe/orqa-slicer/actions/runs/40032840606)).
- The existing native Release build completed successfully. Every required
  portable install-manifest path exists. The Windows executable's help-mode
  process exited with code 0 when explicitly awaited.
- SolidWorks add-in 0.1.0.16 rebuilt successfully into a separate artifact folder.
- Source-only launcher checks now cover fresh Git checkouts nested inside another
  installation, installed workspaces, saved source snapshots and the empty revision
  chooser. These passed locally and in GitHub Actions.
- The Multi-print first actions were visually verified in a separate account-free
  1190×795 preview. Add printer and Load saved cards are visible above the list.
  The native layout build succeeded. The original selected installation remains
  available; the separate preview was closed after inspection.
- Community backend patch application and all three helper hashes match the
  working backend. No additional modified tracked file is outside that patch.
- The current native source matches the pinned OrcaSlicer baseline plus
  integration/native-ui.patch and 39 copied integration source files. Verified
  with scripts/verify-source-capture.ps1 on October 23, 2026.
- scripts/check.ps1 passes the source-only account, cloud printing, connection,
  material, printer catalog, firmware notice, Fast Start, SolidWorks bridge and
  version/settings checks. These checks do not contact printers or accounts.
- GitHub Actions runs that same command on Windows for pushes and pull requests.
- Generated binaries, logs, shortcuts, local settings, tool caches and upstream
  checkouts are excluded. Historical source snapshots and dependency notices stay.

## Remaining acceptance gates

- Complete the independent native build from reconstructed pinned source (running
  with the existing dependency cache), including optional
  community backend and the SolidWorks add-in where prerequisites are installed.
- Audit release packaging from a fresh checkout: launchers, required runtime
  files, add-in installation, version selection, rollback and settings retention.
- Review current UI and documentation together, resolve usability defects and
  record a combined release acceptance checklist.
- Preserve existing hardware evidence and obtain missing user checks for live
  CAD/printing workflows. Multi-print, additional Bambu models and the delayed
  Flashforge bed reading must retain accurate status in release notes.

The update-routing correction is being compiled. An intermediate portable package
started before that correction is superseded and must not be uploaded as the final
release. Final packaging must run after the last native build completes, then be
extracted and inspected before uploading.

The repository migration does not itself prove a polished release. Do not label
the product release-ready based only on source checks or an uploaded commit.
