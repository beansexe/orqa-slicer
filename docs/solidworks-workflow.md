# SolidWorks → Orqa Print

The SolidWorks add-in adds an **Orqa** CommandManager tab for part documents,
with **Choose and print** and **Load in slicer** buttons. Clicking either opens
printer, nozzle profile, material and process selectors. Hovering shows tooltips;
the dialog does not open merely from moving the mouse over a command.

## Install and use

1. Run **Install SolidWorks Add-in.cmd** in the workspace and accept Windows'
   administrator prompt. SolidWorks discovers add-ins through its machine-wide
   registry key. This installs only this add-in; it does not change SolidWorks
   export defaults, machine execution policy or existing macros.
2. Restart SolidWorks or enable **Orqa Print** in **Tools → Add-ins**. Open a part
   and select its **Orqa** ribbon tab.
3. Open the bridge-enabled Orqa revision using **OPEN SLICER**, and connect your
   printer accounts. Keep that Orca instance running; it can be minimized.
4. In SolidWorks, choose the connected printer and its installed nozzle profile,
   then the material and process. **Refresh** rereads the live Orca session.

The lists come from the running slicer, not a second preset catalog. Standard
layer-height profiles (0.14, 0.16, 0.20, 0.24, etc.) appear when installed and
compatible with that printer. The add-in does not invent unsupported profiles.
Both process and material lists offer **Current in Orca (including unsaved
edits)** when compatible. They are read again when exporting, so changes made
after opening the dialog are included when that choice is used.

## Load and adjust

**Load in slicer** exports the active part/configuration as STEP and opens it in
Orca with the selected settings. Once the import succeeds, the SolidWorks popup
closes and focus switches to that Orqa window, restoring it if minimized. A failed
import keeps the popup open with its error. Change orientation, supports, process and
filament settings normally. On another export choose **Current in Orca** for
process and/or material to reuse those edits without creating named presets.

Loading the same part/configuration again replaces the objects imported for it
earlier in this Orca session. Other objects are retained. Review the plate before
printing from the normal slicer UI. Replacement currently resets the replaced
objects' placement; it does not transfer paint, supports, per-object overrides or
manual transforms from the previous geometry. Named SolidWorks configurations
have distinct identities. An unsaved document has a temporary identity until saved.

STEP preserves the CAD geometry until Orca meshes it. Background import uses
Orca's 0.003 mm linear and 0.5 angular deflection defaults. The resulting sliced
3MF contains G-code, plate metadata and a preview. The CAD document is not saved
or renamed. The stable temporary STEP for the same part/configuration is replaced
on each export; request-local files protect an in-progress slice from overwrites.

## Background Print

With Orqa 0.1.0-dev.13 and add-in 0.1.0.15, Fast Start profiles require review
when the part's solid volume is **10 cm³ or greater** (or cannot be measured).
Cancel is the default. Print anyway continues for this print only; for a finished
part, cancel and choose the normal machine profile. The check runs before export
and slicing. Native Orqa printing has the same threshold. Load in slicer does
not warn until you choose to print there.

Confirm that the selected material is loaded on the **external spool**, then
press **Print**. A separate CLI process imports STEP, orients, arranges and slices
using temporary snapshots, and submits via Orca's already-connected account.
It does not raise the slicer window or replace the project currently open there.
The dialog reports submission, not physical print completion. Use Device to
observe the printer. There is no automatic resubmission after a timeout or an
uncertain cloud result.

This first background route supports **one material and one extruder**, through
the existing Bambu/Flashforge cloud routes. **AMS, multiple extruders, per-object
settings and painted supports require Load in slicer and the native print
workflow.** Selecting a material profile does not load physical filament. Orca
must already be running and signed in; the add-in does not create a second
account session or run a persistent tray service. Background orientation is
automatic; use Load in slicer when orientation matters.

Temporary snapshots and exports live under
`%LOCALAPPDATA%\OrqaSolidWorks\bridge\<process-and-session>`. They are removed
when that Orca session shuts down normally. A crash can leave temporary files;
old files are not replayed into a new session. Saving a normal 3MF project in
Orca is the way to keep geometry/settings beyond the session. No permanent user
presets are created by exporting or background printing.

## Build and uninstall

Build with `scripts/build-solidworks.ps1` (uses the installed SolidWorks API
assemblies and .NET Framework compiler). Run `tests/build-solidworks-tests.ps1`
for the mailbox/quoting tests. Native bridge sources are synchronized by
`scripts/apply-integration.ps1` and included in `integration/native-ui.patch`.

To uninstall, close SolidWorks, then run
`artifacts/solidworks-0.1.0.16/install.ps1 -Uninstall` in administrator PowerShell.
The installer registers the built DLL at its current workspace location; keep
that folder in place while installed.

The add-in is installed and loaded into SolidWorks, with part commands enabled
and the active document's identity/modified flag preserved. The native bridge
has discovered three connected printers and exported their compatible preset
snapshots in a live check. Actual CAD export inside SolidWorks, unsaved-edit
round trips and physical print delivery still need the user acceptance test.
