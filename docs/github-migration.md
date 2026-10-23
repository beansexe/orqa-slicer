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

- Verify a fresh source checkout and complete native build, including optional
  community backend and the SolidWorks add-in where prerequisites are installed.
- Audit release packaging from a fresh checkout: launchers, required runtime
  files, add-in installation, version selection, rollback and settings retention.
- Review current UI and documentation together, resolve usability defects and
  record a combined release acceptance checklist.
- Preserve existing hardware evidence and obtain missing user checks for live
  CAD/printing workflows. Multi-print, additional Bambu models and the delayed
  Flashforge bed reading must retain accurate status in release notes.

The repository migration does not itself prove a polished release. Do not label
the product release-ready based only on source checks or an uploaded commit.
