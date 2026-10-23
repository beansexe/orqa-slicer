# SolidWorks add-in 0.1.0.16 — dialog focus

The part export chooser and Fast Start warning no longer create separate taskbar
windows. The chooser cannot be minimized separately. Closing the chooser returns
focus to its SolidWorks frame after Windows Forms finishes closing the window.
The Fast Start warning remains owned by the chooser. Successful Load in slicer
keeps its intentional handoff to Orqa, with no competing SolidWorks activation.

This is an add-in update only; Orqa revision 13 continues to provide the Fast Start
volume check. Installation disables startup of add-in 15. The new assembly can
be loaded without restarting SolidWorks or saving the current part.

Validation: add-in build and 24 existing bridge checks. No export, slice or print
is needed to exercise closing the chooser.
