# First sign-in after the preview is built

These steps apply after a complete preview has been published and selected.
The source preview is not a runnable application until that build finishes.

1. Double-click `Launch-Slicer.cmd` in this workspace. It opens the version
   selected by the local version manager.
2. Complete Orca's normal first-run setup. Enable its Bambu networking
   integration when offered. Select the machine profiles you use.
3. Open **Printer accounts** in the top menu.
4. In the Bambu Lab section, click **Bambu account**. The account dialog closes
   and Orca opens its existing Bambu sign-in flow. Sign in there with your Bambu
   account and complete any verification it requests.
5. Reopen **Printer accounts**. The Bambu section should say **Signed in to
   Bambu Lab**.
6. In the Flashforge section, enter the email address or phone number and
   password you use in Orca-Flashforge. Click **Sign in to Flashforge**. The
   password field clears immediately. Wait for the connection to finish.
7. The Flashforge section should show a signed-in status and the printers
   returned by that account. Click **Refresh printers** to check the list.
   Bambu should still show as signed in.

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
Uncheck it for a session-only login. Each published preview currently has its
own account settings, so a new preview may require signing in again.
If you later turn remembered sign-in back on after disabling it, sign out and
sign in again to save a new session. Sign-out disables automatic restoration
before removing the saved token, including when the credential store is locked.

For the first verification, close and reopen Printer accounts, then restart
the preview and check that remembered accounts return. Signing out of Flashforge
should leave Bambu signed in. Sign back in to Flashforge, then sign out of Bambu
and verify that Flashforge remains signed in.

Enter credentials only in the application. If sign-in fails, report the status
shown in the dialog and which provider failed; do not send passwords or tokens.
Real sign-in, session restoration, and simultaneous accounts have not yet been
tested against your accounts.

This milestone adds account access and the Flashforge account's printer list.
Flashforge wireless job submission, the combined printer picker, and firmware
notifications are later milestones. Existing Orca Bambu printing remains in
place and still needs a printer regression check.
