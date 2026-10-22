# How REZ.exe aims in VR (static analysis, 2026-10-22)

Ghidra 11.0.3 headless on a copy of `REZ\REZ.exe` (version.txt 2.8, image base `0x140000000`);
nothing was run. The project and a full decompile (`out/decomp.txt`, 7,526 functions) were left
in that session's scratch folder (`rez_aim_re\`). "Certain" means read in the decompile;
"inferred" means not.

## Summary

- **Pose source (certain):** the OpenVR manager is `VR::VRManager_OVR` (vtable `0x14041fee0`,
  instance `DAT_140a1e360`). Its slot +0x48 (`FUN_14006dc00`) calls
  `WaitGetPoses(this+0x20, 16, NULL, 0)` once per drawn frame, then `FUN_14006e170` builds the
  head, eye and aim data. Nothing calls `GetDeviceToAbsoluteTrackingPose`, `GetLastPoses`,
  `GetLastPoseForTrackedDeviceIndex`, `GetControllerStateWithPose` or `PollNextEventWithPose`.
  Velocities and `eTrackingResult` aren't read.
- **Which controller (certain):** only the "selected" one, at `this+0xa08`. Slot 0 is the
  LeftHand role and slot 1 is RightHand; the default is 0. A touchpad press (button 32) on a
  controller selects it. If the tracking system is `"oculus"`, any button press does.
- **Aim axis (certain):** the controller's −Z, with no correction, unless the tracking-system
  name is `"oculus"`. In that case the aim is tilted −39.4° about the controller's local X
  (−0.68766 rad, float at `0x14044f21c`).
- **Option (certain):** "VR control type" is cfg dword `0x98`. The user's file has **0**.
  - **0 = type-1** ("head movement or motion controller"): the controller's world yaw/pitch is
    added on top of the head view. Beyond ±30° yaw, a re-centre offset (`this+0xc70`) absorbs
    half of the excess each frame and never resets.
  - **1 = type-2** ("motion controller only"): the controller is taken relative to the head, so
    the cursor follows where the controller points in the world.
  - **2 = type-3** ("head only"): the controller angles are zeroed in gameplay.

  Dword `0x9c` is the VR style (1 standard, 2 advanced, 3 dynamic); the user has 2. The menu
  text comes from `rez_vr_message.txt` in `REZ.dat`.
- **Rate:** the angles update every drawn frame (certain). The cursor, the lock-on box and lock
  acquisition run in the 60 Hz tick (certain).
- **For the mod:** use type-2, and rewrite both hand controllers' rotation as `R_hmd · R_gaze`,
  taken from the same `WaitGetPoses` call, keeping `bPoseIsValid` true. Leave the HMD entry
  alone. If the tracking system is `"oculus"`, post-multiply by `Rx(+0.68766)`.

## OpenVR use

- `FUN_14006c960` gets IVRSystem_015 (also cached in `DAT_140a1e3b0`). `FUN_14006c9f0` gets
  IVRCompositor_015 (`DAT_140a1e3c8`), and fetches it again whenever `VR_GetInitToken()`
  changes. `FUN_14006ca80` gets IVRApplications_006. IVRRenderModels_005 is fetched but never
  called. `SetTrackingSpace` is never called, so poses are in Standing space (inferred).
- **IVRSystem methods used:**
  - +0x00 GetRecommendedRenderTargetSize
  - +0x08 GetProjectionMatrix
  - +0x10 GetProjectionRaw
  - +0x20 GetEyeToHeadTransform
  - +0x88 GetTrackedDeviceIndexForControllerRole (1 = left, 2 = right)
  - +0x98 GetTrackedDeviceClass
  - +0xd0 GetStringTrackedDeviceProperty (device 0, prop 1000 = TrackingSystemName)
  - +0xe0 PollNextEvent
  - +0x100 GetControllerState (idx, buf, 0x40)
  - +0x110 TriggerHapticPulse
  - +0x138 IsInputFocusCapturedByAnotherProcess
- **IVRCompositor methods used:** +0x10 WaitGetPoses and +0x28 Submit.

### VRManager_OVR slots

45 slots; the IVRManager base vtable is at `0x14041fb20`.

| Slot | Function | What it does |
|---|---|---|
| +0x10 | `FUN_14006cfa0` | init; sets `+0xc58` = (tracking system == "oculus") |
| +0x28 | `FUN_14006dbb0` | haptics |
| +0x30 | `FUN_14006eea0` | controller poll: role → index, GetControllerState, PollNextEvent → button bits |
| +0x40 | `FUN_14006abd0` | is VR active |
| +0x48 | `FUN_14006dc00` | WaitGetPoses, then `FUN_14006e170` |
| +0x68 / +0x70 | `FUN_14006dea0` / `deb0` | get / set the current eye |
| +0x90 / +0x98 / +0xa0 | `FUN_14006f5a0` / `f5b0` / `c760` | head-translation scale: get / set / set by index 0..2 → {1, 2, 4} (cfg dword `0x3c`) |
| +0xb8 | `FUN_14006dfa0` | head view matrix `this+0xa48` (the inverse HMD pose; translation relative to the first pose × scale) |
| +0xc0 | `FUN_14006dfb0` | eye view `this+0xa88 + eye*0x40` |
| +0xc8 | — | eye projection |
| +0xe0 | `FUN_14006dd00` | FOV |
| +0xf8 | `FUN_14006e0c0` | Submit |
| +0x100 | `FUN_14006f4c0` | the selected controller's button record (`this+0x960 + sel*0x50`) |
| +0x110 | `FUN_14006f510` | set the selected controller |
| +0x118 | `FUN_14006f530` | isOculus |
| +0x120 | `FUN_140069db0` | returns 3 (the input-device code for VR controllers) |
| +0x148 | `FUN_14006f540` | haptic strength |

### Manager layout

- `+0x20`: `TrackedDevicePose_t[16]`, stride 0x50.
- `+0x520`: 16 row-vector 4×4 copies.
- `+0x960 + i*0x50`: controller i. The button bits are at +4, the device index at +0xc, and the
  `VRControllerState_t` at +0x10.
- `+0xa08`: the selected slot.
- `+0xc58`: isOculus.
- `+0xc70`: the type-1 yaw offset.
- `+0xc74`: the head-translation scale.
- `+0xc78` / `+0xc7c`: the origin-captured flag and the origin.

## Controller choice

`FUN_14006eea0` runs once per tick. For i = 0 and 1 it gets the device index for role i+1,
calls GetControllerState, and handles button events 200–203.

- **Non-oculus path** (`FUN_14006ec50`): button 32 (touchpad) on either controller selects it.
  Buttons 33 (trigger), 1 (menu) and 2 (grip) count only on the selected controller.
- **Oculus path** (`FUN_14006ed90`): any press selects that controller.

`InputMgr` (`FUN_1401a4620`) can also restore a saved selection.

The aim branch in `FUN_14006e170` runs only if all of these hold:

- the device class is Controller (2);
- `DAT_1404cc8a8 == 3` (the last input came from a VR controller button);
- this device is the selected controller;
- `bPoseIsValid` is true.

The offset matrix is the identity unless `+0xc58 == 1`. In that case it is Rx(−0.68766), giving
`aim = R_ctrl · Rx(−39.4°) · (0, 0, −1)`.

## The cursor

Per frame, in `FUN_14006e170` (trimmed):

```c
menu = FUN_140068be0();  type = (byte)DAT_14052265c;   // 0 / 1 / 2
M = (menu || type == 1) ? ctrl * headView : ctrl;       // FUN_140069100(this+0xa48, out, ctrl)
M = offset * M;
if (!menu && type == 0) M = M * RotY(c70);
d = menu ? 640 : 400;
P = M.row3 - d * M.row2;  k = |d / P.z|;  X = k*P.x;  Y = k*P.y;  Z = -k*P.z;
DAT_1409ac29c = (int)(640 + X);  DAT_1409ac294 = (int)(360 - Y);   // menu pointer
DAT_1409ac298 = atan2f(X, Z);  /* yaw */   DAT_1409ac18c = atan2f(Y, Z);  /* pitch */
if (!menu) {
  if (type == 0 && |yaw| > 0.5236) c70 += (yaw - limit) * 0.5;
  DAT_140498f73 = (type == 0) ? 1 : (|yaw| <= 90°);
  if (type == 2) yaw = pitch = 0;
}
```

The controller's position barely matters: P sits 400 m (640 m in menus) along the ray.

### Per tick, in `FUN_14014f550` (Player Camera task, `FUN_14014f920`)

1. If `DAT_1404cc8a8 == 3`, convert the angles to 1/65536 turn.
2. Compute the screen position: `x = tan(yaw)·sx + cx`, `y = −tan(pitch)·sy + cy`.
3. `FUN_14014f2e0` unprojects that point through the view (rail × head × eye) to the 3D cursor
   `DAT_1409fbfd0`.

So the cursor angles apply in the head frame: type-1 adds the head and the controller, and in
type-2 the head cancels out. In menus, every type uses the head-relative pointer. Type-3 turns
on camera-follow.

### The cfg file

`CFG/rez-infinite.cfg` sits in the game root: 0x1a8 bytes, copied raw to `DAT_140a12fa0` (read by
`FUN_1401dd1c0`, written by `FUN_1401dd940`). The control type is at offset `0x98` and the style
at `0x9c`; `FUN_14010b500` packs them into `DAT_14052265c`.

### Lock-on

- `FUN_14012a4b0` builds a box of cursor ± 35 px, scaled to the VR render width.
- `FUN_1401291e0` locks one target every `DAT_1409fbfe0` ticks while the button is held. The
  interval depends on cursor speed v (px/tick):
  - v < 10: 4 ticks;
  - 10 ≤ v < 30: `int((1 − v/30)·4 + 1)`;
  - v ≥ 30: 1 tick.

  **Fast sweeps lock faster, so smoothing the gaze slows lock-on.**

## Risks for overriding the pose in WaitGetPoses

WaitGetPoses is the only pose source, and only `mDeviceToAbsoluteTracking` is read.

1. Rewrite both hand controllers: the game uses whichever one is selected.
2. Aiming works only after a VR controller button press. Gamepad, keyboard or mouse input
   switches the cursor to stick aim.
3. Each rewritten controller must report class Controller and `bPoseIsValid`.
4. Type-1 drifts permanently at gaze beyond ±30°, so use type-2.
5. Correct the oculus tilt only when the tracking system is `"oculus"`. SteamVR simulates a Rift
   with Touch controllers for this legacy app, so it may well be.
6. The menu pointer follows gaze too.
7. The cursor updates at 60 Hz, and lock speed depends on cursor speed.
8. Leave the HMD entry alone.
9. SteamController005 is fetched by the standard Steam API init only (inferred); gamepads use
   XInput.

## Still to log at run time

- TrackingSystemName of device 0 (or `+0xc58`).
- The indices for roles 1 and 2, and their class and `bPoseIsValid` on the Frame.
- `+0xa08`, `DAT_1404cc8a8`, the control-type byte `0x14052265c`, and `+0xc70`.
- WaitGetPoses calls per second.
- The legacy button events the Frame controllers send (1, 2, 32, 33).
