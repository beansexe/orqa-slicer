# Development status

The user authorized continued development on October 10, 2026: finish and publish
the account milestone as a revision that needs testing, then proceed with later
features using that interface as the development baseline. Real validation remains
pending until account and printer checks are actually performed.

Current work:

- Account revision 0.1.0-dev.1: native Release build resumed from the cache.
  Correcting two reviewed remembered-session logout cases before publication.
- Next revision: combined printer catalog and model/nozzle profile selection.
- Next revision: Flashforge cloud upload/create/start workflow; preserve and reuse
  Orca's existing Flashforge LAN path and Bambu networking.

Prepared next-revision code (not linked into the account revision):

- integration/printers: provider-qualified catalog and native wx picker.
  Optimized standalone tests and pinned clang-tidy pass. Exact profile/nozzle
  choices, vendor isolation, return to Bambu, and stale/offline gates are covered.
- integration/printing: cloud upload/create/MQTT-start workflow and connection
  lifecycle. Thirty-five workflow simulations and connection fault tests pass;
  pinned source/header/test checks pass. The production archive inspector and
  account/operator bridges remain in development. Actual cloud sending stays
  unavailable until those bridges are completed.
- integration/firmware: authoritative update-availability value model with
  per-device deduplication, dismissal, freshness, and provider isolation. Optimized
  tests and pinned checks pass. Provider/GUI bridges remain pending.

The revision chooser initializes successfully through its validation mode.
Publishing rejects incomplete portable output. Real visual and user workflow
validation remains part of revision testing.

Review notes are saved in docs/cloud-foundation-review.md. The next picker must
verify the actual selected preset after nested dialogs. The MQTT adapter must
check a distinct account-login epoch because the PC client ID survives account
changes. Both fixes are being verified in their owning modules. Proprietary SDK
callback quiescence remains an unverified precondition, so live cloud sending is
kept disconnected.

No account sign-in, printer upload, physical print, or firmware installation has
been performed by this development session.

The user requested a progress save and computer shutdown at about 2% remaining
usage. The usage tool exposes five-hour and weekly included allowances separately
from purchased credits. Purchased credits are zero while included usage remains
available. This session uses the lower remaining included allowance as the
threshold, monitors it during work, and will save all changes and build state
before issuing a normal Windows shutdown without forced application closure.
No rate-limit reset or credit purchase is authorized or being used.
