`FlashNetwork.h` is copied unchanged from FlashForge/Orca-Flashforge at
`137288fa36f005194c72dd72998ca1f9b9223274` (release 1.7.6), under its AGPL-3.0 license.
The adjacent account adapter and GUI integration are also released under AGPL-3.0.
See LICENSE.txt. No Flashforge networking DLL is redistributed here.

The ABI check is intentionally strict: the public reference loader requires
fnet_getVersion() == 3.3.6 and uses FLASHNETWORK9.DAT. Other plugin generations
require matching published headers and server settings, not an ABI override.
