# GitHub migration and release readiness

Repository: https://github.com/beansexe/orqa-slicer (private).

## Verified migration and software checks

- The complete workspace source, integration patches, historical source snapshots,
  tests, build scripts and notices are committed. Local tools, built versions,
  account state and logs remain excluded. The workspace has an authenticated
  `origin` and a repository-local GitHub noreply commit identity.
- Both vendor baseline commits and the optional community baseline were retrieved
  directly from public GitHub. Orca-Flashforge is clean at its locked commit.
  Native source capture reproduces the pinned baseline plus native-ui.patch and
  all 39 integration source files. The community patch and three helper headers
  also capture its working source exactly.
- An independent Windows Release build and installation completed from reconstructed
  pinned application source. Existing dependency binaries and downloaded Git
  objects were reused; this was not a cold dependency rebuild. The optional
  community backend also compiled in a separate build tree from a fresh public
  source checkout, with its dependency source pins checked.
- SolidWorks add-in 0.1.0.16 compiled from both the primary and independent
  workspaces. All 24 bridge checks passed without loading an add-in or editing
  a CAD document.
- Source-only checks passed locally, from a fresh GitHub clone, and on Windows
  GitHub Actions. Coverage includes account adapters, cloud print preparation,
  connection faults, material validation, catalog matching, firmware notices,
  Fast Start policy, bridge protocol and shared-settings/launcher behavior.
- Launcher fixtures cover installed workspaces, independent checkouts nested
  inside another installation, saved source snapshots and an empty revision chooser.
- The Multi-print first actions were visually verified in a separate account-free
  1190×795 preview. Add printer and Load saved cards are visible above the list.
  The explicit update command shows the Orqa prompt with No as the default.
- The temporary UI copy is closed. Revision 13 remains selected, and the
  orcaslicer:// handler was restored to that selected installation on October 26.

## Portable release evidence

The Windows ZIP contains 18,124 entries and is 230,223,582 bytes. Its matching
workspace source snapshot is commit `8aa8151cd826b23ceddd4ffb7cde17d40964af0c`.
Later review commits change documentation and Git metadata, not native source.

SHA256:
`3891326143f2e8191630727851204cc8c41a6fe982452260a90e0ddc245b9983`

- The final archive was generated after the corrected native build completed.
  The preliminary archive was superseded and is not a release asset.
- The ZIP was extracted into a separate folder. Required install-manifest paths,
  executable startup, relocated Python imports, binary hashes, matching Git
  source archive and empty initial settings all passed.
- The extracted executable converted a public STEP fixture into a sliced 3MF.
  G-code, metadata, preview, printer/nozzle matching and wrong-nozzle rejection
  passed the production archive inspector. No print or upload was submitted.
- Bundled community DLL exports, ABI and empty-profile lifecycle passed. Their
  hashes match revision 13's existing components. Independently rebuilt community
  DLLs passed the same ABI/lifecycle check.
- ZIP and checksum uploads were verified against GitHub's reported SHA256 values.
  A companion getting-started guide is attached to the draft release.

## Live acceptance and existing preview limits

Automated checks do not establish that a physical print completed. Preserve the
existing user-confirmed X2D cloud printing/video and Flashforge cloud printing/job
control evidence; do not present other Bambu models or live Multi-print batches
as verified. Delayed Flashforge bed reporting remains unresolved.

The remaining user checks are collected in
[the release acceptance checklist](release-acceptance.md). They cover the actual
CAD chooser/focus/handoff, per-printer settings and saved workspaces, and optional
physical regression testing. Release 14 remains a needs-testing preview until
that live acceptance is recorded.
