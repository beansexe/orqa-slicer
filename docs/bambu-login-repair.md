# Bambu Community Cloud sign-in repair, October 19, 2026

Scope: revision 0.1.0-dev.10 optional networking component only. No native
rebuild, new whole revision, account migration or printer-mode changes.

The Windows OpenSSL curl build does not load Windows certificate roots by
default. Account REST requests therefore failed before an HTTP response, even
though browser-based Google authentication could open. The backend now loads
its existing compiled Mozilla root bundle into each HTTP TLS context when no
explicit CA file is supplied. Peer and hostname verification remain enabled;
this does not use an insecure retry or share a temporary CA file.

Evidence from isolated, credential-free checks:
- Original DLL profile request with a fake credential: HTTP 0, result -19.
- Direct public HTTPS request with the same cached curl: certificate error 60.
- Same request with the existing CA bundle: TLS succeeds, HTTP 404.
- Repaired DLL profile request with the same fake credential: HTTP 401.
- Release component build and existing ABI/load/empty-profile lifecycle check passed.
- Google account sign-in and printer behavior: pending user retest.

Run Apply Bambu Sign-in Fix.cmd while the slicer is closed. The updater checks
component and corresponding-source hashes, stages a replacement and renames
the old community-bambu folder to community-bambu-before-login-fix. It leaves
account data and backend selection untouched. The original BUILD-VERIFICATION
record describes the original publication; BAMBU-LOGIN-FIX.json records the
replacement component hashes. No live account logs or credentials were read.

Rollback with the slicer closed: preserve the current community-bambu folder
under another name, restore community-bambu-before-login-fix to community-bambu,
and remove BAMBU-LOGIN-FIX.json. Standard connection remains selectable in
Printer accounts and does not depend on this component.
