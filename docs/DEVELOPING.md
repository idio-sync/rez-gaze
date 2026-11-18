# Developing rez-gaze

Background: what's known, measured and reverse-engineered is in [findings.md](findings.md) and
[aim-re.md](aim-re.md). The design is in
[superpowers/specs/2026-10-22-rez-gaze-aim-design.md](superpowers/specs/2026-10-22-rez-gaze-aim-design.md).

## Pieces

| Path | What |
|---|---|
| `proxy/` | Stand-in `REZ\openvr_api.dll`. It rewrites the hand controllers' rotation in `WaitGetPoses` so the cursor follows the gaze, and runs calibration, filters and the thumbstick trigger. Rez's own DLL is renamed `openvr_api_real.dll`. All `rez_gaze.ini` keys are documented at the top of `proxy/proxy.cpp`. |
| `helper/` | `REZ\rez_gaze\rez_gaze_helper.exe`. The DLL starts it 10 s after VR is up. It reads gaze through OpenVR's `eyetracking` action and publishes it through shared memory. |
| `shared/` | The shared-memory block (`Local\rez_gaze_<game pid>`, seqlock). |
| `probe/` | `gaze_probe`, the diagnostic tool. Modes: `openvr`, `openxr [--frames]`, `inproc`, `osc`, `replay`, `shm`. |
| `release/` | `package.ps1` (builds the zip), `install.ps1` / `uninstall.ps1`, the default `rez_gaze.ini`, and the zip's `README.txt`. |
| `tests/` | Unit tests, helper smoke tests, installer and package tests, and a harness that runs the DLL without the game or SteamVR. |

## Build and test

`build.cmd` (VS 18 Build Tools + `C:\ninja.exe`) builds everything into `out\`. Then run
`ctest --test-dir out --output-on-failure`. No test needs SteamVR or the headset, and none
starts SteamVR.

To build the way CI does, configure a scratch folder inside vcvars with
`-G "NMake Makefiles" -DREZ_GAZE_VERSION=nightly-test`.

## Releases

`.github/workflows/release.yml` runs on a Windows runner: build, test, package, publish. A
failing build or test publishes nothing.

- **Nightly:** every push to `main` replaces the single **nightly** pre-release
  (`releases/tag/nightly`) with `rez-gaze-nightly-<sha>.zip`.
- **Tagged:** pushing a tag `v*` publishes a release named after it, with notes listing the
  commits since the previous `v*` tag. A tag containing `-` (e.g. `v0.2.0-beta`) becomes a
  pre-release. Tagged releases are never replaced.

  ```
  git tag v0.1.0
  git push origin v0.1.0
  ```

The version (`nightly-<sha>`, the tag, or `dev` locally) is stamped into the DLL and helper
and shown on the first line of their logs. A local zip comes from
`powershell -ExecutionPolicy Bypass -File release\package.ps1 -Out out -Version dev`.

## Installing a dev build by hand

With no Rez running, in `Rez Infinite\REZ\`:

1. Rename `openvr_api.dll` to `openvr_api_real.dll`, first install only. It must be the
   302,880-byte 2021 file. On a case-sensitive folder, also copy it as `openvr_api_real.DLL`.
2. Copy `out\proxy\openvr_api.dll` in.
3. Copy `out\rez_gaze\rez_gaze_helper.exe` and `out\rez_gaze\openvr_api.dll` into `rez_gaze\`.
4. Add `rez_gaze.ini` (`release\rez_gaze.ini` is the default).

## Headset checks

Start SteamVR with the Frame first, then Rez. Logs: `REZ\rez_gaze.log` and
`REZ\rez_gaze\rez_gaze_helper.log`.

1. **`mode=log`.** Press each Frame button. The log should show:
   - the HMD tracking system (`oculus` under SteamVR's legacy simulation);
   - hands `right=… left=…` with class 2;
   - `control type 1 controller only`;
   - `input device 3`;
   - `helper status "running"`.
2. **`mode=head`.** The cursor follows the head; moving the controller does nothing.
3. **`mode=gaze`.** The cursor follows the eyes.
4. **`source=helper` vs `source=osc`.** The 5 s log lines show each source's age.

Not yet checked in the headset:
- **The thumbstick calibration.** The log notes every thumbstick down/up, to confirm that the
  click arrives as button 32.
- **`filter=euro`** against `smooth_ms=40`.
- **`gain_yaw`/`gain_pitch`**, if the cursor falls short of the edges. The 5 s line
  `aim max |yaw| … (DLL) vs … (game)` tells whether the gaze or the game is short.

## Working alongside the frame-rate mod

The frame-rate mod (branch `hfr`, its own worktree) also installs into Rez's `REZ` folder
(`XINPUT1_3.dll`, `rez_hfr.ini`). It doesn't touch OpenVR, and `openvr_api.dll` is this
project's. Before launching Rez, check that the other session isn't running it.
