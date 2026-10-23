# Launcher repair — October 19, 2026

The user launched a saved-source copy under revision 5. That copy searched its own
revision-source folder for a versions directory and reported no selected revision.
Main and saved-source .cmd launchers now walk up to the enclosing live workspace
before listing or launching builds. The chooser script also resolves its owner
when invoked directly from a saved snapshot. The chooser lists newer folders first.

Root-launcher and revision-5 snapshot resolution checks passed. The saved chooser
initialized with all 10 published revisions; revision 10 remains selected. No native
slicer binaries, accounts, sessions or printer settings changed. No new revision
was created. Root shortcuts OPEN SLICER and MANAGE SLICER VERSIONS point to the
main launchers. Saved source launchers/scripts were updated in all ten snapshots.
