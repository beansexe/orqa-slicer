# Start using Orqa

## Open the portable preview

Extract the entire Windows ZIP into a writable folder and double-click
**START ORQA.cmd**. Keep the previous installation for rollback. This standalone
copy keeps its presets and accounts in its own `data_dir`; it contains no saved
accounts. Existing source-workspace users can keep using their version manager
and shared settings instead.

Complete the normal profile setup. Choose the printer models and nozzle sizes
actually installed on your machines. Open **Prepare** to load a model.

## Connect printer accounts

Open the menu icon beside **File**, then **Printer accounts**.

- **Bambu:** choose **Bambu account** and finish Bambu's sign-in flow. The home
  screen's **Orca Cloud Login** is a different account service.
- **Community Cloud:** the optional files are included in the complete portable
  preview. In Printer accounts, choose **Cloud connection options…**, read the
  experimental connection prompt, and enable it if wanted. Restart Orqa, then
  sign in to Bambu again. Standard and Community Cloud sessions are separate.
  User testing has confirmed X2D cloud printing and video with this connection;
  other Bambu models retain the verification limits in the release notes.
- **Flashforge:** sign in using the email/phone and password used in
  Orca-Flashforge. If the plug-in is unavailable, use **Choose plugin** to select
  the separately installed `FlashNetwork.dll` and `FLASHNETWORK9.DAT`. The adapter
  accepts ABI 3.3.6. The password field clears after sign-in; remembered sessions
  use Windows credential storage.

Check that the expected printer appears before preparing a job. Select its
matching nozzle, filament and process settings, then inspect Preview and the
vendor's send confirmation before printing.

## Use Multi-print

In Prepare, enable **Multi-print** beside Print. Choose **+ Add printer to plate**
for each target. Select a card to edit that printer's settings using the normal
controls. Objects remain shared, while each printer gets its own validated slice.

The batch opens each vendor's send confirmation. Review the printer and AMS
mapping there. Save the Multi-print workspace when needed, and keep its 3MF and
companion printer-card file together. Open the saved 3MF before using
**Load saved printer cards…**. See the
[full checklist](revisions/0.1.0-dev.11-user-tests.md) for live batch testing.

## Use the SolidWorks add-in

The add-in is built separately on a PC with SolidWorks and its API assemblies.
Follow the [SolidWorks workflow](solidworks-workflow.md) in the matching workspace
source. Keep only the intended Orqa instance running so the add-in can identify
its bridge session, then use **Load in slicer** or **Choose and print**.

Use Load in slicer for AMS or multiple extruders. Background printing currently
supports one material and one extruder on an external spool. A Fast Start profile
asks for explicit confirmation at 10 cm³ or greater; Cancel is the default.

## Update or return to an earlier version

**Help → Check for Updates** opens an Orqa prompt offering this project's GitHub
releases page. Updates are deliberate; pulling source changes does not replace an
installed executable. Keep the earlier folder and its settings until the new
version is tested. Source-workspace users can select an earlier saved revision
through **Manage-Versions.cmd**.
