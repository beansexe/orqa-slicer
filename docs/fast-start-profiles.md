# Fast Start: already-loaded nozzle, no cleaning macros

Orqa revision 0.1.0-dev.13 and SolidWorks add-in 0.1.0.15 warn before printing
with Fast Start when solid model volume reaches **10 cm³**. Cancel is selected
by default; Print anyway continues for that print only. Use a normal machine
profile for finished parts. Loading into the slicer does not trigger the warning.

Restart Orqa, select the matching **- Fast Start** machine preset and re-slice.
Existing sliced files retain their previous startup. Profiles cover X1 Carbon,
X2D and H2S with 0.2/0.4/0.6/0.8 mm nozzles. Installed in the launcher's shared
settings for both local and account presets. Stock profiles are unchanged.

Use one already-loaded material/nozzle with an unchanged, calibrated setup.
Loaded state is not detected automatically. Use the normal profile for loading,
material/nozzle changes, multicolor work or calibration. Later toolchange code is
inherited and is outside this single-material startup workflow.

Heating starts before homing. After reference setup and temperature waits,
H2S/X2D print an explicit 40 mm prime line using G1, with extrusion calculated
from nozzle size and layer height. There is no physical wipe on these models:
no manual wiper coordinates have been verified, so none were invented. X1C
retains its explicit coordinate-based final wiper pass and final prime line.
No startup bin purge, material-load cycle, bed survey or vibration sweep remains.

Warm-repeat update: startup M191 S0 chamber-cooling waits and M140 S0 bed
shutdowns are removed. Ventilation/airduct control remains, but no longer blocks
startup to cool the enclosure. Bed heating runs during homing. Positive chamber
heating waits for high-temperature materials and M190 bed-ready checks remain;
end-of-print heater shutdown remains. The latter is needed for cold starts, and
its exact cooling behavior depends on printer firmware. If a remaining wait
shows a 35 C BED target, check the selected physical plate and filament plate
temperature settings: those are distinct from the removed chamber-cooling wait.

Start, end and timelapse overrides exclude all G150 variants, G151, G383 variants,
G390 variants, G28.14/G28.140 and G130. These include known wipe/parking calls and
uncertain calibration/priming macros; their removal is not a claim that every
listed command performs wiping. H2S/X2D priming uses the initial homed Z reference
and saved nozzle calibration rather than firmware priming-height routines.
Basic G28 homing remains. Other vendor commands needed for machine state,
compensation, thermal management, physical hotend selection and detection remain.
Their firmware internals are not available for a guarantee of zero hidden motion.

End code leaves material loaded instead of unloading to AMS, and retains heater
shutdown and stock Z clearance. Timelapse motion is disabled. Do not enable
optional send-time calibration or timelapse for this workflow. Correct physical
plate/nozzle selection and an initially clean nozzle remain necessary. First-layer
height and adhesion need verification after removing the native reference macros.

The installed Bambu PLA Basic and Generic PLA filament starts contain comments
or cooling commands, not nozzle cleaning. A new filament profile would not remove
the printer's macro-driven cleaning.

Regenerate/install with scripts/make-fast-start-profiles.ps1 -Install. JSON copies,
source/output hashes and backups are in artifacts/fast-start-profiles. Independently
edited installed profiles are protected. No application rebuild is required.

Validation: 12 profiles passed banned-command checks across start/end/timelapse,
JSON and conditional balance, explicit prime-line preservation, heater shutdown,
and installed-file checksum checks. No physical printing or timing was performed.
Firmware-internal homing behavior remains unverified.
