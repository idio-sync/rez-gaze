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
