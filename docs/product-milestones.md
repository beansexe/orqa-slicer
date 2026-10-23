# Consolidated product milestones

The user requested larger feature batches on October 15, 2026, to reduce the
number of handoffs and conserve credits. Revision 10 is the selected test
build; revision 9 remains available for rollback. Do not publish a new revision merely for a message or small cosmetic fix;
bundle those changes into the next useful feature milestone. Urgent regressions
in already-working functionality can still justify a separate repair.

## 1. Everyday Flashforge cloud printing and device controls

Deliver one coherent device workflow: clear printer selection, live job filename,
progress and layer information, cloud pause/resume/cancel where supported by the
vendor interface, and actionable connection/command feedback. Improve the large
snapshot-style device page as part of this same batch. Preserve Bambu selection,
accounts and the already-confirmed Flashforge cloud submission path.

The existing SDK declares cloud job controls, but declaration alone is not proof
of correct routing or remote acceptance. Follow the pinned vendor implementation,
bind controls to current authenticated account/printer/job identity, refresh live
state before dispatch and avoid automatic retries of uncertain commands. Confirm
cancellation with the user in the product. Do not add LAN as a prerequisite.

Handoff: one native build containing the complete usable workflow, with a short
hardware checklist. Development checks cover changed behavior and packaging;
the user validates physical commands and cloud acceptance.

## 2. Bambu cloud integration and media

Resolve the restricted-command path as a substantial integration batch. Evaluate
the supported vendor handoff and community networking alternatives for the X2D,
X1C and H2S. Include loaded-filament control, cloud print routing and camera
compatibility in the investigation. A replacement must retain sign-in, status,
light controls, device selection and rollback. Keep provider integration separate
from account secrets and preserve Flashforge functionality.

Handoff: clearly label which controls are integrated directly, which use an
official companion application, and which remain unsupported. A clearer error
message alone does not complete this milestone. Full vendor parity is a target,
not a promise that an external authorization restriction can be removed.

## 3. Consolidated release and updates

Combine remaining usability work, account recovery, printer/nozzle selection,
firmware-notice presentation and distribution into a release candidate. Provide
simple local updates and rollback without requiring a full source ZIP download.
Preserve shared settings and accounts. Firmware notices must distinguish explicit
availability from unknown state; installation uses the vendor-supported route.

Handoff: a coherent release candidate, concise instructions and one combined
regression checklist across all four machines. Test only what changed plus the
essential account/selection/printing paths. Do not repeat broad suites without a
reason or present untested vendor functionality as validated.

## Working cadence

Make routine implementation choices autonomously. Ask the user only for missing
information or account/hardware actions that cannot be completed independently.
Keep working features available through the local version manager. Record user
evidence separately from development checks. Hardware testing belongs to the user;
build, source and packaging checks remain part of development.

