# Findings so far (2026-10-21)

## Rez Infinite

- PC version (2017; the install here is `S:\_Steam Library\steamapps\common\Rez Infinite`,
  version.txt 2.8) is **two programs** behind the `Rez-infinite.exe` launcher:
  - `REZ\REZ.exe`: Areas 1–5, Monstars' own engine (5.5 MB). Its own `REZ\openvr_api.dll`.
    Asks for `IVRSystem_015`, `IVRCompositor_015`, `IVRApplications_006`, `IVRRenderModels_005`.
  - `AX\REZAX\Binaries\Win64\REZAX-Win64-Shipping.exe`: Area X, Unreal Engine 4. Loads
    `AX\Engine\Binaries\ThirdParty\OpenVR\OpenVRv1_0_2\Win64\openvr_api.dll` (OpenVR 1.0.2).
    Asks for `IVRSystem_012`, `IVRCompositor_016`, and others.
  - Neither asks for `IVRInput`: both use the legacy controller API (`GetControllerState`) and
    take poses from `IVRCompositor::WaitGetPoses`.
- In VR it aims with the head, or with motion controllers (Vive wands, Touch): the reticle
  follows where the controller points.
- The PS VR2 version (2023, Area X moved to UE5) added eye-only aiming: look at enemies to mark
  them while holding fire, release to shoot. Reviewers found it accurate. So the design is proven.
  The PC version never got it.
- No existing Rez mods or UE4SS work turned up.

## Steam Frame gaze on the PC (SteamVR, checked in the installed files)

- SteamVR's OpenXR runtime (`bin/vrclient_x64.dll`) contains `XR_EXT_eye_gaze_interaction`
  and `XR_MND_headless` (a session with no graphics, for programs that run beside a game).
- OpenVR has eye tracking too, since SDK 2.15.6 (2026-03-27): `IVRInput::GetEyeTrackingDataRelativeToNow`
  and `GetEyeTrackingDataForNextFrame`, returning `VREyeTrackingData_t` (active, valid, tracked,
  gaze origin, gaze target), read through an action of type `"eyetracking"`. Valve's Frame input
  page documents only that much: no binding example.
- Binding guess: the streaming driver (`drivers/vrlink`) creates an `/eyetracking` input
  component, and SteamVR's binding parser takes an `"eyetracking"` array of `{path, output}`. The
  streamed HMD's controller type is `remote_hmd` (`frame_hmd` exists too). So the probe binds
  `/user/head/eyetracking` on both, and `--bind` overrides it.
- Permission: a Valve-side reply (SteamVR-for-Linux #946, 2026-10-03) says the PC gets
  `XR_EXT_eye_gaze_interaction` only "when permission is explicitly granted in Steam Link's VR
  settings to send eye tracking data to other apps".

## The user's Vitrine3D phase 0 (standalone, native on the Frame, 2026-10-20)

From `Vitrine3D/docs/reference/steam-frame-native-viewer-research-2026-10-20.md`, O1:
`supportsEyeGazeInteraction = true` with no prompt; tracked on 100% of frames once the session
was FOCUSED; a new gaze vector every frame; the tracked bit never dropped, **blinks included**
(so blinks must be detected some other way, or not at all). Angle to head-forward: median
7–11°, p90 16–24°, max 29–45°. **Gaze flowed only once the session was FOCUSED**, 8–9 s after
`xrBeginSession` with the headset worn.

That last point is the main risk: OpenXR gives input only to the focused session, and a helper
beside Rez won't be focused. Hence the probe's `inproc` mode.

## Probe run 1 (2026-10-22, PC streaming, SteamVR 2.18.4, no game)

- **No gaze from either route.** OpenVR action `active 0` all run; OpenXR `supportsEyeGazeInteraction
  = true`, profile bound to `/interaction_profiles/ext/eye_gaze_interaction`, session FOCUSED,
  but the action stayed inactive and the gaze space never located. Cause not yet known: headset
  not worn, or Steam Link's eye-data permission off (no PC-side setting exists; nothing in the
  vrlink log). vrlink does log "Client HMD type (5) supports eye tracking, creating input component".
- The HMD's controller type here is `frame_hmd` (not `remote_hmd`); the probe's binding file
  for it loaded "successfully", the action still inactive, so `/user/head/eyetracking` is unproven.
- **OpenVR and OpenXR can't share a process**: in inproc mode xrCreateSession failed with
  "Client connection in an invalid state [1], while attempting to transition application to
  VrApplication_OpenXRScene". A mod inside Rez can only use OpenVR.
- **XR_MND_headless isn't headless on SteamVR**: the session's app type changes to
  `VRApplication_OpenXRScene` and goes FOCUSED, i.e. it becomes the scene app. Beside Rez it
  would compete with the game.
- **Setting an action manifest flips the app from legacy to SteamVR Input** (vrclient log:
  "Determined this is a steamvr_input app", legacy simulation cleared). `GetControllerState`
  still returned true afterwards, but no buttons were pressed, so whether Rez would keep its
  controls is still open.

## Probe run 2 (2026-10-22, headset worn, the user's eye setting on)

- `openxr` again: nothing. `openxr --frames` (a D3D11 session submitting a cleared projection
  layer at 144 Hz): FOCUSED for ~28 s, eye-gaze profile bound, action never active, no gaze.
  So a rendering, focused OpenXR app gets no gaze either: the block is not focus or rendering.
- **The cause: a PC-side Steam Link setting.** `drivers/vrlink/resources/settings/default.vrsettings`
  has `"shareEyeTrackingData": false` ("Share eye tracking data to other apps on this PC") and
  `"useOSC": false` ("Enable OSC"), out port 9000 (alt 9015), in port 9016. The PC's
  `steamvr.vrsettings` sets neither, so both are off. They live under SteamVR Settings > Steam Link,
  not in the headset's own settings.
- **vrlink can send eye data over OSC** (`driver_vrlink.dll` strings): VRChat's
  `/tracking/eye/CenterVecFull` (gaze vector) and `/tracking/eye/EyesClosedAmount` (blinks),
  plus `/avatar/parameters/LeftEyeX` etc. and `/sl/eyeTrackedGazePoint`. OSC goes to
  127.0.0.1 over UDP, so any process can read it whichever app has focus. That removes the
  focus problem and the legacy-input problem: a helper (or the proxy DLL itself) just listens.
  `gaze_probe osc` listens on 9000.

## Probe run 3 (2026-10-22, after turning on both Steam Link settings): gaze works

With `shareEyeTrackingData`, `useOSC` (and `useOSCFace`) on, written to `steamvr.vrsettings`
under `driver_vrlink`, and vrlink logging "OSC was (re)initialized successfully. In Port: 9016,
Out Port: 9000":

| Run | Beside | Tracked | New directions | Off head-forward (median / p90 / max) |
|---|---|---|---|---|
| `openxr --frames`, 30 s | nothing (it is the scene app) | 100% once FOCUSED (4.5 s in) | 122/s (one per 144 Hz frame) | 21.5 / 25.8 / 34.7° |
| `openvr` overlay, 15 s | SteamVR Home | 100% from 0.3 s | 200/s (every poll; predicted) | 15.2 / 21.7 / 36.9° |
| `osc`, 20 s | SteamVR Home | all valid | 57–72/s | 11.1 / 17.8 / 17.9° |
| `openvr` overlay, 30 s | **Rez (REZ.exe, scene app, vr=1)** | **100%** from 0.2 s | 186/s | 14.2 / 24.7 / 35.5° |
| `osc`, 30 s (same time) | **Rez** | all valid | 57/s | 15.5 / 24.2 / 30.8° |

- **Feasibility answered: a separate helper gets the gaze while Rez runs**, both through OpenVR
  as an overlay app (binding `/user/head/eyetracking` on `frame_hmd` is right) and over OSC.
  Neither needs focus, and neither touches Rez's process or its legacy input.
- OSC sends: `/tracking/eye/CenterVecFull ,fff` (unnormalised, length ~1.08, +Z forward),
  `/sl/eyeTrackedGazePoint ,fff` (the same with -Z forward), `/tracking/eye/EyesClosedAmount ,f`,
  `/sl/xrfb/facew/EyesClosedL|R ,f`, and VRChat's `/avatar/parameters/{Left,Right}Eye{X,Y,Lid,...}`.
  The probe's "tracked" for OSC uses EyesClosedAmount < 0.5, which is wrong: the values don't
  read as plain 0 open / 1 closed (LeftEyeLid 1.0 with EyesClosedL 1.0 and the right eye 0.27
  in one packet). Blink handling needs its own look before the mod relies on it.
- OpenXR's tracked flag stays set through blinks (as Vitrine3D found on the device); OpenVR's
  probably too. OSC is the only source with lid data.
- Not yet measured: latency of each route (gaze change → value), and accuracy against known
  targets.

## Helper milestone (2026-10-22, offline: SteamVR off, user away)

Branch `gaze-helper`; plan `docs/superpowers/plans/2026-10-22-gaze-helper.md`.

- **`rez_gaze_helper.exe`.** An OpenVR overlay app. It reads the `eyetracking` action and
  publishes head-relative gaze to `Local\rez_gaze_<game pid>` at about 205 Hz. It exits within
  milliseconds of the game exiting.
- **The DLL starts it** on the first `IVRSystem_015`. The DLL creates the block first, so a
  helper that fails still leaves its status for the DLL's log.
- **Source choice.** The DLL uses the helper if it published within 250 ms, otherwise OSC,
  otherwise straight ahead (`source=auto|helper|osc`).
- **Tested without the game or SteamVR:**
  - 19 unit tests: seqlock, aim math against the decompiled cursor formula, source choice and
    the OSC parser;
  - helper smoke runs: background, overlay, and `--fake`;
  - a harness with a fake `openvr_api_real.dll`.
- **Found while testing:**
  - an overlay helper's `VR_Init` starts SteamVR if it isn't running. The helper now checks
    with a background init first. The broken build's first test run left a stray
    `vrmonitor.exe` showing "Unexpected SteamVR Error".
  - SteamVR running without a connected headset refuses the overlay app with
    `WirelessHmdNotConnected` after about 4 s.
- **Not yet run against real OpenVR:** the helper's running path (SteamVR on, headset on).
  That's the first item of the next headset session.

## Planned mod (unchanged by the probe's outcome, except where gaze comes from)

- A proxy `openvr_api.dll`, one copy in each program's DLL folder (above), forwarding every
  export to the real one. It intercepts `VR_GetGenericInterface` for the two compositor versions
  (`IVRCompositor_015` and `_016`) and the two system versions (`IVRSystem_015` and `_012`), and
  rewrites `WaitGetPoses` / `GetDeviceToAbsoluteTrackingPose` results: the aiming controller's
  orientation becomes the head pose turned by the gaze direction. Buttons stay on the real
  controller. Gaze is applied head-relative, so OpenXR and OpenVR spaces never need lining up.
- Gaze comes from outside the game (run 3): an OpenVR overlay helper or OSC, handed to the
  proxy DLL (shared memory for the helper; or the DLL listens to OSC itself on a thread). Rez's
  process never sets an action manifest, so its legacy input is untouched.
- Feel: hold through dropouts, light smoothing of fixation jitter, maybe snap to the nearest
  enemy within ~2°.
- Under Proton/FEX on the headset, Wine loads the game-folder DLL the same way.
