rez-gaze: eye-gaze aiming for Rez Infinite (classic Rez, PC VR) on the Steam Frame
===================================================================================

Look at enemies to aim; hold and release fire as usual. Experimental: see VERSION.txt.

Needs
-----
- Rez Infinite on Steam (classic Rez, REZ\REZ.exe; Area X isn't supported yet).
- A Steam Frame streaming from this PC through Steam Link.
- SteamVR > Settings > Steam Link: "Share eye tracking data to other apps on this PC" on
  ("Enable OSC" too, for the fallback gaze source).

Install
-------
1. Close Rez.
2. Right-click install.ps1 > Run with PowerShell, or in a terminal:
     powershell -ExecutionPolicy Bypass -File install.ps1
   It finds Rez through Steam; otherwise add -RezDir "<...>\Rez Infinite\REZ".
   Rez's own openvr_api.dll is kept as openvr_api_real.dll. The script stops without changing
   anything if that DLL isn't the one it expects. Updating keeps your rez_gaze.ini.
3. In Rez: Options > VR > control type "motion controller only".

Play
----
Start SteamVR with the Frame first, then Rez. Press a controller button once in game so Rez uses
the VR controls (Rez switches away if it sees mouse or keyboard input; press a button to switch
back).

Calibrate
---------
In a level, hold BOTH thumbsticks pressed down for 2 seconds; the controllers buzz once and the
cursor freezes. Keep your eyes on the cursor and slowly move your head around it (circles, then
up/down and left/right) for about 15 s. A double buzz means the calibration was saved and eye
aiming resumes by itself. Holding both thumbsticks again during calibration cancels it (long
buzz). With one controller, set calib_combo=one in rez_gaze.ini (either thumbstick, 3 s).
Redo it whenever the headset sits differently. Eye tracking works best without prescription
lens inserts.

Settings and log
----------------
REZ\rez_gaze.ini (explained inside) and REZ\rez_gaze.log; the helper logs to
REZ\rez_gaze\rez_gaze_helper.log.

Uninstall
---------
Close Rez and run uninstall.ps1. Rez's own openvr_api.dll goes back; rez_gaze.ini and the logs
stay.

openvr_api.dll in rez_gaze\ is Valve's OpenVR SDK 2.15.6 runtime (LICENSE-OpenVR.txt).
