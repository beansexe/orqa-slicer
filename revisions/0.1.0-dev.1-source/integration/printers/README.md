# Printer catalog

The catalog is a value model for one combined printer picker. It keeps the Bambu
and Flashforge account lists separate internally and exposes stable IDs such as
`bambu:<native device ID>` and `flashforge:<native device ID>`. Names and model
names affect presentation and profile matching, never identity. A complete
snapshot replaces only its provider's devices. A failed refresh keeps the previous
devices visible as stale; signing out clears only that provider's devices.

The model contains no SDK calls, credentials, account storage, print submission or
GUI code. The separate native `PrinterPickerDialog` copies the live Bambu cloud
list and a caller-owned Flashforge snapshot, presents exact profile choices,
and routes Bambu selection through the existing device manager. Flashforge
selection saves its own provider-qualified selection for profile editing.
The optional account-navigation callback runs after the modal picker closes.
The Flashforge LAN setup button selects and validates the exact profile, closes
the picker, then opens the same native `PhysicalPrinterDialog` used by Orca's
Connection button. Its host type remains operator-selected; choosing Flashforge
reveals stock discovery, address, serial-number and checkCode/API-key settings.
No discovery or job submission starts automatically. The picker has no send
button and is not part of the account-only preview. Capability flags and
readiness must come from a working adapter. Unknown capabilities, unknown status,
an unavailable route, offline or busy printers, errors and expired snapshots all
block the preliminary send gate. The native send workflow must still validate
the sliced project, materials, device security and current connection before any
physical printing. Snapshot freshness is an adapter-supplied deadline, rather
than an assumed universal vendor polling interval.

Profile matching uses the loaded preset's exact name as its selectable ID,
resolved `printer_model` and `nozzle_diameter` values as metadata, and the provider
as part of the match. Templates and machine-model metadata are not selectable
profiles. Known aliases include X2D/N6, X1C/BL-P001, H2S/O1S and Adventurer 5M/AD5M.
The Bambu machine IDs are recorded in the pinned Orca source's BBL machine-model
JSON files. Adventurer 5M Pro remains a distinct model. Unrecognized names match
only their case-folded exact spelling; no fuzzy guess chooses a different machine.
Missing nozzle telemetry leaves every matching variant visible and requires a
choice when multiple profiles match. Reported nozzle telemetry filters candidates
and a manual choice cannot override a mismatch. Multiple custom/system profiles
at the same nozzle size still require a choice.

## Native picker integration

`DeviceCore/DevManager.h` scopes Bambu machine enumeration to a printer agent.
The GUI adapter copies values from `get_my_cloud_machine_list("bbl")`, using the native
`MachineObject::dev_id`, `dev_name`, `printer_type`, `is_online()`, `can_print()`
and reported nozzle information. The copy belongs on the existing device/UI
thread; the catalog never retains `MachineObject*` pointers. Bambu account
readiness must be explicitly scoped to `BBL_CLOUD_PROVIDER`.

Flashforge device enumeration is available through the existing independent
`FlashforgeAccount::devices()`. Its IDs already begin with `flashforge:`; remove
exactly that known prefix when producing a `CatalogPrinter::native_id`, rather
than qualifying twice or stripping arbitrary colon-containing native IDs.
Its current account adapter exposes only a model/name/status string, so wireless
capability and send-route availability remain unknown/false until the Flashforge
wireless transport is implemented and its actual readiness contract is verified.
Display status text should not be guessed into a ready-to-print boolean.

The native picker shows vendor, device name, model, readiness and exact profile
choices. It displays Flashforge's raw reported status without inferring print
readiness, and shows when each printer list was captured. It copies visible,
loaded printer presets from `PresetBundle::printers` into `CatalogProfile` values.
Orca's loader already excludes abstract, uninstantiated machine templates.
On selection it routes the exact preset name through the current printer tab's
`select_preset(...)` flow, as
`PresetComboBoxes.cpp` already does, so unsaved changes and filament/process
compatibility continue to be handled by the slicer. Do not call
`get_similar_printer_preset()` to silently choose a nozzle variant. The profile
flow can reactivate Bambu's agent when returning from a Flashforge profile; the
picker validates the account and device identity before profile selection, then
checks the active agent and ownership again afterward. If the Monitor panel is
built, it uses that panel's normal `on_select_printer()` handler. Otherwise it
uses `DeviceManager::set_selected_machine(native_id)` and the native
`MonitorPanel::on_machine_selected()` sidebar/calibration callback. If the native
list is unavailable, the UI directs the user to reconnect through the Device tab.
Flashforge selection
must remain in its separate state and must never be inserted into Bambu's
`MachineObject` list or passed to Bambu's print job.
The caller can provide `FlashforgePickerSnapshot::selection_is_current()` to
validate the account owner/generation and printer ID after a nested unsaved
settings prompt. A failed check permits profile editing but does not persist a
new printer binding.
After `Tab::select_preset()` returns, the picker also checks the actual edited
preset name and freshly enumerated profile model/nozzle metadata. A removed or
changed preset cannot silently bind the previous preset's machine. LAN setup
uses the same checks and confirms the selected profile/session again before
opening the native Connection dialog.

Async adapters must discard results after logout/account changes and apply
snapshots serially on the UI thread. The caller owns that operation/session
generation; the value model itself has no async work. Keep existing native Bambu
LAN selection and printing flows active alongside this initial cloud-account
picker. Extending the catalog to independent LAN routes should be explicit about
route authentication rather than assuming a cloud sign-in grants LAN access.

## Verification

Run `tests/build-printer-catalog-tests.cmd` for the optimized standalone C++17
build and behavior tests. It writes only `artifacts/printer-catalog/`. The tests
cover identity separation, exact nozzle selection, ambiguous presets, rejected
malformed snapshots, refresh failure, expiry, provider logout and conservative
send readiness. They use copied values and perform no network or printer calls.
`tests/check-printer-catalog.cmd` checks the source, standalone header and tests
with the workspace's pinned clang-tidy and Orca's include rules.
`tests/check-printer-picker.cmd` derives an isolated compile database from the
existing account-enabled `upstream/OrcaSlicer/build-tidy/compile_commands.json`
and checks the native picker source/header. It does not configure or modify the
main build. GUI runtime and real-account selection still require user testing.
