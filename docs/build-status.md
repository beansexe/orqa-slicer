# Build checkpoint

Revision **0.1.0-dev.10** is published and selected as **needs testing**. Native
Release build/install, source and package matching, fresh integration patch
reproduction, CLI/Python startup and the ten-revision chooser passed. The optional
community networking/camera DLLs compiled and passed local ABI/export/load/lifecycle
checks. Synthetic Windows encryption and shutdown ownership checks passed.

Close the current preview and open Launch-Slicer.cmd. In Printer accounts, choose
Cloud connection options, enable Community Cloud, restart, then sign in to Bambu.
It uses a separate protected session and preserves standard Bambu and Flashforge
sessions. Real Bambu printer controls, camera and cloud printing remain unverified.
The standard backend is the default; revision 9 is available for rollback.

The bundled Flashforge nozzle correction matches the vendor's Adventurer 5M field
mapping. Hardware confirmation is pending. Bed temperature delay is unresolved.
The user confirmed revision 9 cloud pause/resume/cancel, layer/time details and
switching to Bambu. Bambu light/AMS worked with the standard plug-in; movement and
filament edits were restricted. No system-wide camera registration was changed.

The optional DLLs include source/notices in community-bambu. Reusable builds use
scripts/build-community-bambu.ps1 and scripts/stage-community-bambu.ps1. Settings
remain in revision 1's data_dir. See the revision notes for scope and test steps.

October 19: Bambu Community Cloud HTTPS certificate repair installed in revision
10. See docs/bambu-login-repair.md. Original component retained in
community-bambu-before-login-fix; replacement hashes in BAMBU-LOGIN-FIX.json.
Google sign-in remains pending user retest; synthetic HTTPS and ABI checks passed.

October 19: X2D homing and live video confirmed by user. Print start returned
create_task HTTP 403. Small all-region task-authentication repair installed in
revision 10; native executable unchanged. Release build, ABI/empty-profile
lifecycle and installed source/DLL checksums passed. Real print retest pending.
See docs/bambu-cloud-print-repair.md. Prior component retained for rollback.

October 19: revision 0.1.0-dev.11 built, published and selected, needs user testing.
Adds a visible Multi-print toggle, left printer/plate cards, independent native
printer/process/filament/plate settings, per-target validation and slices, a
sequential mixed-brand cloud batch with native send review, and saved workspace
companions. Repaired Bambu Community Cloud components and prior backend choice
are included. No real job was sent by development. See revision11 user checks.
