# Flashforge temperature reporting

Revision 9 user testing reports missing temperature values during preparation,
with bed actual/target appearing around the purge line. Layer/time data already
arrives correctly. Cloud pause/resume/cancel and provider switching are confirmed.

The current live path polls fnet_getWanDevProductDetail over authenticated HTTP
every five seconds while the Device page is visible and monitoring is enabled.
It does not retain an MQTT subscription for monitoring. MQTT connections currently
exist only for individual print/control operations. The vendor client consumes
FNET_CONN_READ_DEVICE_DETAIL pushes as well as HTTP detail observations; that
provides a concrete alternate source to investigate if HTTP values lag.

Nozzle fields currently prefer nozzleTemps/nozzleTargetTemps element zero when
the array is present. Otherwise they use rightTemp/rightTargetTemp. Bed values
come from platTemp/platTargetTemp. Range checks omit invalid values. The display
currently hides both values when either one is missing. That presentation should
be corrected independently of identifying the missing telemetry source.

Before changing telemetry selection, distinguish omitted/invalid data, legitimate
zero targets and stale HTTP values. Do not invent targets from a slicing preset,
treat zero automatically as missing or show old values as freshly observed. Compare
the official client at the same print stage if the user can observe it. Do not
read or log raw account payloads, tokens or URLs to diagnose this issue.

Bundle the temperature correction with the next substantial integration build;
revision 9 remains the selected build while its working controls are retained.

Source finding: the vendor NewTempInputPanel::UpdateTempature path uses rightTemp/rightTargetTemp for the Adventurer 5M; nozzle arrays are used for C5/C5P. The adapter now follows that 5M mapping, with a fixture for allocated zero-filled arrays. This source change is pending the next combined build and has not been hardware-validated. The bed still uses the same platTemp/platTargetTemp fields as the vendor. No bed fix is established.
