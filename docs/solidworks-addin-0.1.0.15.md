# SolidWorks add-in 0.1.0.15

Adds a Fast Start confirmation before background printing a part of 10 cm³ or
more. It uses the complete active configuration's solid volume, independent of
the current selection. Cancel is the default, including Enter and closing the
dialog. Print anyway approves only the current prepared settings snapshot.
Smaller parts and normal profiles proceed without the warning. Unknown volume
also requires review. Load in slicer keeps its existing focus handoff behavior.

Requires native Orqa revision 0.1.0-dev.13 or later. The warning check is required
by that bridge for Fast Start prints; older native versions are rejected for
background printing by this add-in with an update message.

Built as Bridge15 with a distinct COM class so it can be loaded without closing
the active SolidWorks document. Installer disables startup of prior add-in
revisions. Prior revision artifacts remain available for rollback.
