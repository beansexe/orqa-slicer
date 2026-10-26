# Preview 14 acceptance checklist

## Development checks completed

- GitHub source history, branch workflow, public source pins and source capture.
- Independent native and community backend builds using the existing dependency cache.
- Add-in compilation and 24 bridge checks.
- Fresh-checkout and installed-workspace launcher/settings fixtures.
- Multi-print first-action layout at 1190×795 and the Orqa update prompt.
- Extracted ZIP startup, Python relocation, source provenance, binary hashes and empty settings.
- Public STEP to sliced 3MF, G-code, metadata, preview and nozzle validation.
- Community DLL exports, ABI and empty-profile lifecycle; remote ZIP checksum.

No automated check logs in, sends a job, moves a printer or installs the add-in.

## User checks in the live workflow

Save any work before switching versions. Keep the previous installation and
settings. Use only the intended Orqa instance with the SolidWorks add-in.

1. In SolidWorks, open the Orqa chooser and close it. Confirm focus returns to
   SolidWorks and no separate taskbar window remains. Use Load in slicer with a
   known part and confirm its intended configuration appears in Orqa, without
   changing the original document's saved state.
2. In Prepare, enable Multi-print at your normal window size. Confirm Add printer
   and Load saved cards are visible. Add two printer cards, use different settings,
   switch between them, and confirm each keeps its own settings.
3. Save the Multi-print workspace and keep its 3MF and card file together. Reopen
   the 3MF, load its cards, and confirm the assignments/settings return. Toggle
   Multi-print off and confirm the ordinary single-printer controls return.
4. When ready for hardware testing, use a small known job and review each vendor
   confirmation. Check actual printer/nozzle/material routing and cancellation.
   Follow the detailed [batch checklist](revisions/0.1.0-dev.11-user-tests.md).

Record the preview/add-in version, affected step and visible error for any failure.
Do not send account credentials or private model files when reporting a problem.

Existing limits: other Bambu models and full live batch behavior are not confirmed;
Flashforge bed readings can be delayed. Background CAD printing is currently one
material/one extruder on an external spool. Use Load in slicer for AMS or multiple
extruders. These limits are not changed by the GitHub migration.
