# rez-gaze

Eye-gaze aiming for Rez Infinite on PC (classic Rez, `REZ\REZ.exe`) with the Steam Frame: look at
enemies to mark them, as the PS VR2 version's eye-only mode does. In progress.

- What's known, measured and reverse-engineered: [docs/findings.md](docs/findings.md),
  [docs/aim-re.md](docs/aim-re.md).
- The design: [docs/superpowers/specs/2026-10-22-rez-gaze-aim-design.md](docs/superpowers/specs/2026-10-22-rez-gaze-aim-design.md).

## Pieces

| Path | What |
|---|---|
| `proxy/` | Stand-in `REZ\openvr_api.dll`. It rewrites the hand controllers' rotation in `WaitGetPoses` so the cursor follows the gaze. Rez's own DLL is renamed `openvr_api_real.dll`. |
| `helper/` | `REZ\rez_gaze\rez_gaze_helper.exe`. The DLL starts it. It reads gaze through OpenVR's `eyetracking` action and publishes it through shared memory. |
| `shared/` | The shared-memory block (`Local\rez_gaze_<game pid>`, seqlock). |
| `probe/` | `gaze_probe`, the diagnostic tool. Modes: `openvr`, `openxr [--frames]`, `inproc`, `osc`, `replay`, `shm`. |
| `tests/` | Unit tests, helper smoke tests, and a harness that runs the DLL without the game or SteamVR. |

## Build and test

`build.cmd` (VS 18 Build Tools + `C:\ninja.exe`) builds everything into `out\`. Then run
`ctest --test-dir out --output-on-failure`. All tests run without SteamVR or the headset, and
none of them starts SteamVR.

## Install (classic Rez only)

With no Rez running, in `Rez Infinite\REZ\`:

1. Rename `openvr_api.dll` to `openvr_api_real.dll`. Only on the first install: check it's the
   302,880-byte 2021 file.
2. Copy `out\proxy\openvr_api.dll` in.
3. Copy `out\rez_gaze\rez_gaze_helper.exe` and `out\rez_gaze\openvr_api.dll` into
   `REZ\rez_gaze\`.
4. Add `rez_gaze.ini`. Its keys are documented at the top of `proxy/proxy.cpp`; start with
   `mode=log`.

To uninstall, delete those files and rename `openvr_api_real.dll` back.

## Headset checklist (PC streaming)

Settings, once:

- **SteamVR > Settings > Steam Link:** "Share eye tracking data to other apps on this PC"
  (needed by every route). Also "Enable OSC" if you want the OSC fallback.
- **In Rez:** Options > VR > control type **"motion controller only"**. The default "head
  movement or motion controller" drifts once gaze is past 30°.

Start SteamVR with the Frame first, then Rez. Logs: `REZ\rez_gaze.log` (the DLL) and
`REZ\rez_gaze\rez_gaze_helper.log`.

1. **`mode=log`.** Press each Frame button. The log should show:
   - the HMD tracking system;
   - hands `right=… left=…` with class 2;
   - `control type 1 controller only`;
   - `input device 3`;
   - `helper status "running"`.
2. **`mode=head`.** The cursor follows the head, and moving the controller does nothing.
3. **`mode=gaze`.** The cursor follows the eyes. Play an area.
4. **`source=helper`, then `source=osc`.** Compare how they feel. The 5 s log lines show each
   source's age.
5. **Display-rate cursor.** This comes from the frame-rate mod (branch `hfr`, `c91cc6f`).
   Toggle `cursor=0` / `cursor=1` in `REZ\rez_hfr.ini` (re-read every 2 s): first with
   `mode=head`, then with gaze. With `cursor=1` the cursor should move at display rate instead
   of stepping at 60 Hz, and lock-on should behave the same.

The frame-rate mod (branch `hfr`) also uses Rez's folder. Tell that session before launching.
