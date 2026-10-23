# Accessing the Flashforge printer

These controls are included in 0.1.0-dev.3 and later. Earlier account previews
list Flashforge printers only inside Printer accounts.

Close the old preview and open the workspace's Launch-Slicer.cmd. Open the main
menu, choose Printer accounts, and wait for the remembered Flashforge sign-in
and printer list. Select the Adventurer 5M and click Open device, or double-click
its row. You can also open the Flashforge tab directly after the account is ready.

The Flashforge page shows the model and last status received with the account's
printer list. Refresh printer list fetches that list again. In revision 4, check
Monitor selected printer through Flashforge Cloud to request temperatures,
progress, device status, and firmware while the page is visible. The live values
have their own update age. Stealth Mode must be disabled for cloud requests.
Use Printer accounts if a session needs reconnecting. Video and firmware update
announcements remain deferred.

To select the machine for slicing, open Choose printer in the main menu. Select
the Flashforge printer and the nozzle profile actually installed. If no profile
matches, click Add printer profiles, enable the exact Flashforge model/nozzle in
Orca's normal setup wizard, and reopen Choose printer.

Set up Flashforge LAN printing opens Orca's existing Connection dialog. Choose
the Flashforge host type and use discovery or the printer's network settings to
provide the requested address, serial number, and checkCode/API key. Save the
connection and use Orca's normal slicing and send workflow. Choosing a profile
or opening Connection setup does not start a print.

Revision 4 adds Send current plate via cloud on the Flashforge page. Select the
exact printer/nozzle through Choose printer, slice a single-material plate, and
open Preview first. The send dialog defaults to file delivery; explicitly check
Start printing after delivery to request a print. This cloud path needs no LAN
address. Follow docs/revisions/0.1.0-dev.4.md for the first tests and cancellation
behavior. Real cloud sending is still awaiting user validation.

For Bambu, choose its printer/profile to return to the native Device view.
The two vendor accounts remain independent.
