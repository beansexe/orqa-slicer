# First sign-in after the preview is built

These steps apply to the account previews, after a complete revision is published
and selected in the local version manager.
The source preview is not a runnable application until that build finishes.

1. Double-click `Launch-Slicer.cmd` in this workspace. It opens the version
   selected by the local version manager.
2. Complete Orca's normal first-run setup. Enable its Bambu networking
   integration when offered. Select the profiles for your Bambu X2D, X1 Carbon,
   H2S and Flashforge Adventurer 5M, with the nozzle sizes actually installed.
3. Open Orca's main menu (the menu containing **Preferences**) and choose
   **Printer accounts**.
4. In the Bambu Lab section, click **Bambu account**. The account dialog closes
   and Orca opens its existing Bambu Lab sign-in flow. If you normally use Google
   in Bambu Studio, choose the **Google** option on this Bambu Lab page (it may
   appear under other sign-in methods). Complete sign-in in the browser with the
   same Google account, then return to the slicer. Keep the slicer running during
   browser sign-in so it can receive the completion callback. The preview already
   includes this browser handoff; no new build is required to try it.
   Email/password sign-in remains available through Bambu's page as well.
5. Reopen **Printer accounts**. The Bambu section should say **Signed in to
   Bambu Lab**. In 0.1.0-dev.2, the dialog separately reports whether the live
   Bambu cloud connection is connected or disconnected. A remembered account
   session alone does not establish that the printer connection is working.
6. In the Flashforge section, enter the email address or phone number and
   password you use in Orca-Flashforge. Click **Sign in to Flashforge**. The
   password field clears immediately. Wait for the connection to finish; the
   dialog's controls are disabled while an account operation is running. Use
   your account password, rather than an SMS verification code.
7. The Flashforge section should show a signed-in status and the printers
   returned by that account. Click **Refresh printers** to check the list.
   Bambu should still show as signed in.

Use **Printer accounts → Bambu account** for your Bambu printers. The home
screen's **Orca Cloud Login** signs into a separate Orca profile service; its
Google option does not sign into the Bambu account that owns your printers.
The Google button itself is supplied by Bambu's live sign-in page. Its availability
depends on that page and the selected login region; if it is absent or the browser
does not return successfully, note the visible error and which step failed.
Source inspection confirms the external-browser and Bambu callback paths are
present in 0.1.0-dev.1. A live Google sign-in has not yet been verified.

Flashforge remains a separate account: this preview's Flashforge adapter uses
the vendor's email/phone-and-password sign-in, with its own saved session.

The preview detects the Flashforge networking plugin installed on this PC.
If it reports an unavailable plugin, click **Choose plugin** and select these
two existing files in sequence:

```text
C:\Program Files\Flashforge\Orca-Flashforge\FlashNetwork.dll
C:\Program Files\Flashforge\Orca-Flashforge\resources\data\FLASHNETWORK9.DAT
```

Only the matching 3.3.6 plugin is accepted. A different version requires an
adapter update. Do not replace the vendor application's files to make it match.

**Keep me signed in using the system credential store** saves the Flashforge
refresh token through Windows credential storage. It does not save the password.
Uncheck it for a session-only login. By default each preview has its own data
folder and account settings. The 0.1.0-dev.2 patch instead uses the original
0.1.0-dev.1 settings folder through its saved settingsVersion reference.
The Flashforge credential key includes the data folder's path, so preserving
that path also preserves its credential-store lookup. A separate new data folder
or moving the original folder may require another sign-in. The token itself is
stored in Windows credential storage.
If you later turn remembered sign-in back on after disabling it, sign out and
sign in again to save a new session. Sign-out disables automatic restoration
before removing the saved token, including when the credential store is locked.

For the first verification, leave remembered sign-in enabled, close and reopen
Printer accounts, then restart the preview and open Printer accounts again.
Flashforge checks its saved session when this dialog opens; wait for its status
and printer list to finish refreshing. Signing out of Flashforge
should leave Bambu signed in. Sign back in to Flashforge, then sign out of Bambu
and verify that Flashforge remains signed in.

To check a session-only login, uncheck **Keep me signed in**, restart the preview,
and open Printer accounts. Flashforge should remain signed out. For an offline
check, open Printer accounts without internet and try Flashforge sign-out. A
remembered session may show a connection failure; its Sign out button can still
clear the local session. Once internet returns, reopen Printer accounts to retry
a remembered session you did not sign out of. If the preview reports that account
settings could not be saved, make its data folder writable and retry the action.

Enter credentials only in the application. If sign-in fails, report the status
shown in the dialog and which provider failed; do not send passwords or tokens.
On October 14, 2026, you reported successful sign-in to both accounts. The sign-in
method was not specified, so Google sign-in is not separately marked verified.
The later restart report confirms Flashforge restoration and its Adventurer 5M
listing. Bambu retained a session indicator and could list printers, but showed
inconsistent account labels and a server connection failure. On 0.1.0-dev.2,
live Bambu Device status worked after leaving Stealth Mode through Orca Cloud,
then signing out of Bambu and back in. Video is not working and is deferred.
Retention across another restart and physical print operation remain unverified.
The patch reuses 0.1.0-dev.1's settings folder to retain existing sign-ins.

This milestone adds account access and the Flashforge account's printer list.
Flashforge cloud wireless job submission from this new account panel, the
combined printer picker, and firmware notifications are later milestones for the
initial account builds. The 0.1.0-dev.3 device/selection revision adds the
Flashforge page and picker; see `flashforge-devices.md`. Cloud job submission,
live Flashforge monitoring, and firmware notices remain later work.
Existing Orca Bambu printing remains in
place and still needs a printer regression check.
