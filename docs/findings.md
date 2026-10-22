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

## Planned mod (unchanged by the probe's outcome, except where gaze comes from)

- A proxy `openvr_api.dll`, one copy in each program's DLL folder (above), forwarding every
  export to the real one. It intercepts `VR_GetGenericInterface` for the two compositor versions
  (`IVRCompositor_015` and `_016`) and the two system versions (`IVRSystem_015` and `_012`), and
  rewrites `WaitGetPoses` / `GetDeviceToAbsoluteTrackingPose` results: the aiming controller's
  orientation becomes the head pose turned by the gaze direction. Buttons stay on the real
  controller. Gaze is applied head-relative, so OpenXR and OpenVR spaces never need lining up.
- Because both programs use legacy input, gaze read inside the game via `IVRInput` may switch
  their controller input off (SteamVR treats an app that sets an action manifest as an action
  app). The probe's `--legacy-check` tests exactly that.
- Feel: hold through dropouts, light smoothing of fixation jitter, maybe snap to the nearest
  enemy within ~2°.
- Under Proton/FEX on the headset, Wine loads the game-folder DLL the same way.
