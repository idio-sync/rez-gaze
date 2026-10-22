# rez-gaze

Goal: a Rez Infinite (PC, Unreal Engine 4, OpenVR) mod that aims with the Steam Frame's eye
tracking: look at enemies to lock on, as the PS VR2 version's eye-only mode does.

This repository is at step 0: `gaze_probe`, which answers whether a mod can get the gaze at all
while Rez is the running game. See [docs/findings.md](docs/findings.md) for what is known so far
and the planned mod.

## Build

`build.cmd` (VS 18 Build Tools + `C:\ninja.exe`) builds `out\gaze_probe.exe` and copies
`openvr_api.dll` beside it. OpenXR's loader is loaded at run time from SteamVR's folder.

## Test runs (PC streaming to the Frame)

Before you start: in the Steam Link VR settings on the headset, turn on **send eye tracking
data to other apps**. Valve says PC apps get no gaze without it. Then start SteamVR with the
Frame connected and put the headset **on**: in phase 0, the Vitrine3D viewer saw gaze only once
the headset was worn, 8–9 s after its session started.

Each run prints one line a second. Look at `tracked` (samples with live gaze that second) and
`new` (new gaze directions a second, about the tracker's rate). Each run ends with a summary,
and a CSV goes to `out\logs\`. Ctrl+C stops a run.

1. **Baseline, no game:** does gaze reach the PC at all?
   `gaze_probe inproc --seconds 60 --legacy-check`
   This registers as the game (scene app) and reads gaze via OpenVR and OpenXR in one process.
   Look around; press a controller button or two for the legacy check.
2. **Beside Rez, OpenVR:** start Rez Infinite, get into Area X or a level, then in a second
   window:
   `gaze_probe openvr --seconds 60`
3. **Beside Rez, OpenXR:**
   `gaze_probe openxr --seconds 60`
4. If 2 shows `active 0` throughout, try `--app background`, and binding variants:
   `--bind /user/head/input/eyetracking`, `--bind /user/head`.

Send me the console output of each, or the CSVs.

What the answers decide:

| Result | Then the mod |
|---|---|
| 2 or 3 tracks beside Rez | reads gaze in a helper program, hands it to a proxy `openvr_api.dll` in Rez's folder |
| only 1 tracks, and the legacy check still sees buttons after the manifest | reads gaze inside Rez itself (the proxy DLL does the IVRInput calls) |
| only 1 tracks, legacy input stops | reads gaze inside Rez through OpenXR (if 1's OpenXR half tracked), or needs another route |
| 1 doesn't track | gaze isn't reaching the PC: the permission toggle, or a SteamVR issue |

Running Rez on the headset itself (Proton/FEX) is the other option; the proxy DLL works the same
under Wine, but the probe would need an on-device build.
