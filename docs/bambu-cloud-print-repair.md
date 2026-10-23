# X2D Community Cloud print authorization repair

User evidence on October 19, 2026: Google sign-in repair allowed X2D homing and
live video. Cloud print registration failed at create_task with HTTP 403,
native error -2120. No successful Bambu cloud print has been confirmed.

The pinned community backend defaults task proof-of-possession headers to the
China region only. Its [upstream release notes](https://github.com/persano/open-bambu-networking-plugin/releases/tag/v0.2.29)
report that adding these headers resolved the same HTTP 403 on a Chinese-cloud
printer. This is a candidate cause for the user's X2D, not proof of the server's
reason for this particular denial.

This build enables the existing task-proof path in all Community Cloud regions.
Before any upload it checks that signing material is usable. If not, it starts
the backend's existing asynchronous initialization and tells the user to wait
and retry. The final dispatch also refuses to send an unsigned task request.
A 403 now includes only a numeric API code, when present, plus an authorization
message. It does not expose server bodies, tokens or signed URLs. No automatic
job retry or alternate LAN dispatch was added. The HTTPS login repair remains.

Delivery: small component update for revision 10, with complete corresponding
source and checksums in updates/bambu-print-auth-fix-2026-10-19. No native slicer
rebuild or new whole revision. Apply Bambu Cloud Print Fix.cmd requires the
slicer to be closed; it saves the prior component in
versions/0.1.0-dev.10/community-bambu-before-print-auth-fix. Account data and
backend choice are preserved. BAMBU-PRINT-AUTH-FIX.json identifies the new DLLs.
Original publication and login-fix manifests remain historical records.

User test: reopen the selected revision, confirm Community Cloud and select the
X2D, then send one suitable sliced plate. If authentication is being prepared,
wait briefly and retry once. If HTTP 403 remains, report the new numeric API
code if shown. Check the printer's actual status before submitting a new job.
No real accounts, printers or print endpoints were exercised by development.
