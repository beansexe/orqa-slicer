# SolidWorks add-in 0.1.0.14 — switch to Orqa after loading

Used with the existing native Orqa revision 12. A successful Load in slicer now
closes its owned SolidWorks popup and foregrounds the exact Orqa process used for
the import. Minimized Orqa windows are restored. Foreground permission is granted
from the user's original click before asynchronous import work begins. Focus is
handed off after closing the owned popup, so closing it cannot pull focus back
to SolidWorks. Background Print behavior is unchanged.

The repaired session-discovery behavior from add-in 13 is retained. The update
uses a separate COM/assembly identity to avoid SolidWorks retaining the prior
unsigned assembly in its running .NET AppDomain. Earlier add-in startup entries
are disabled by the installer.

Build and all 16 bridge regression checks passed. The update was installed and
loaded into the running SolidWorks session; active document identity and its
unsaved flag were preserved. No CAD export or physical print was performed in
this update's validation.
