# rez-gaze

Aim in **Rez Infinite** with your eyes on the **Steam Frame**: look at enemies to lock on, hold
and release fire as usual. It works like the eye-tracking mode of the PS VR2 version.

Experimental. It works with classic Rez (Areas 1–5) in PC VR, with the Frame streaming from
your PC. Area X and playing directly on the headset aren't supported yet.

## What you need

- Rez Infinite on Steam (PC).
- A Steam Frame connected to your PC through Steam Link.
- Windows.

## Install

1. Download the zip from **Releases**: the latest `v…` release, or **Nightly** for the newest
   build.
2. Unzip it anywhere and close Rez.
3. Right-click `install.ps1` → **Run with PowerShell**.

The installer finds Rez through Steam. It keeps Rez's own `openvr_api.dll` as
`openvr_api_real.dll`, and changes nothing if that file isn't the one it expects. Updating the
same way keeps your settings.

## One-time setup

- **SteamVR → Settings → Steam Link:** turn on **Share eye tracking data to other apps on this
  PC**. Without it the mod gets no eye data.
- **Rez → Options → VR:** set the control type to **motion controller only**.

## Playing

1. Start SteamVR with the Frame connected, then start Rez.
2. Press a controller button once so Rez uses the VR controls.
3. Look at enemies while holding fire, and release to shoot.

## Calibrating

Do this once, and again whenever the headset sits differently:

1. In a level, hold **both thumbsticks pressed down for 2 seconds**. The controllers buzz and
   the cursor freezes.
2. Keep your eyes on the cursor and slowly move your head around it (circles, then up/down
   and left/right) for about 15 seconds.
3. A **double buzz** means the calibration is saved, and eye aiming resumes.

Holding both thumbsticks again during calibration cancels it (long buzz). With one controller,
set `calib_combo=one` in the settings file and hold either thumbstick for 3 seconds.

## Settings

The settings file is `rez_gaze.ini` in Rez's `REZ` folder, with comments inside. Changes take
effect within a second, even while playing. The ones you're likely to want:

| Setting | What it does |
|---|---|
| `smooth_ms=40` | Steadies the cursor. Lower is snappier, higher is steadier. |
| `filter=euro` | An alternative to `smooth_ms`: steady while your eyes rest, quick when they jump. |
| `yaw`, `pitch` | Nudge the aim, in degrees (positive = right, up). Calibration replaces these. |
| `mode` | `gaze` aims with your eyes; `log` turns the mod off without uninstalling. |

## Troubleshooting

- **The cursor ignores your eyes.** Rez switches to mouse/keyboard controls if it sees any
  mouse or keyboard input. Press a controller button to switch back.
- **The aim is suddenly off after you adjusted the headset.** Re-seat the headset and
  calibrate again.
- **No eye aiming at all.** Check the Steam Link setting above, and start SteamVR before Rez.
- **Rez stays black.** Set `helper=0` in the settings file, then tell us. The mod then reads
  your eyes through Steam Link's OSC output instead, which also needs **Enable OSC** in the
  Steam Link settings.
- **With prescription lens inserts** the tracking can be less accurate; calibrating helps.
- The log is `rez_gaze.log` in the `REZ` folder; include it when reporting a problem.

## Uninstall

Close Rez and run `uninstall.ps1` from the zip. Rez's own file goes back; your settings file
and the log stay.

---

Building, testing and releasing: [docs/DEVELOPING.md](docs/DEVELOPING.md).
