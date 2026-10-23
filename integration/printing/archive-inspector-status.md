Saved development checkpoint; this inspector is not wired into a slicer build.

FlashforgeArchiveInspector.hpp/.cpp contain a production inspector draft using
the pinned miniz reader, Expat XML parser and libpng decoder. It expects sliced
Orca/Orca-Flashforge 3MF metadata: slice_info.config plate index, printer_model_id
and a single nozzle_diameters value; model_settings.config plater_id and the exact
Metadata/plate_<one-based>.gcode reference. It streams and CRC-checks selected
G-code, requires real motion commands, decodes the external PNG within bounds,
rejects duplicate/aliased ZIP entries and duplicate plate metadata, and compares
file sizes, modification times and supplied real-byte MD5 hashes before/after.
DTD parsing is disabled. Archive, entry, XML, PNG, filename, nesting and miniz
allocation sizes are bounded.

Status: inspector files are saved but not compiled, code-checked or fixture-tested.
Do not enable them until focused tests with real miniz-generated archives pass.
Needed tests include valid selected plate, model-only 3MF, missing G-code, wrong
model/nozzle/index, duplicate ZIP entries/metadata, malformed XML/PNG, corrupted
CRC, oversized entries and file mutation during the supplied hash callback.
Root must provide the existing libslic3r::bbl_calc_md5 wrapper; no new hash
implementation is included. Multi-nozzle archives are deliberately unsupported.

The connection adapter separately requires a nonzero account_epoch in every fresh
session snapshot. Increment it on logout/login or account replacement, preserve it
on token refresh. Open captures the epoch; callbacks/prepare/publish verify it and
publish checks again after the SDK call. Account-only binaries are unaffected.
