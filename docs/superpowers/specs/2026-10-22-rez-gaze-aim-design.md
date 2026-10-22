# Rez Infinite eye-gaze aiming — design (draft, 2026-10-22)

Status: draft for the user's review. Items marked **(my call)** are decisions made without the
user; items under "Open questions" need the user's answer.

## Intent

Play Rez Infinite on PC in VR, streamed to a Steam Frame, aiming the lock-on cursor with the
eyes: look at enemies while holding fire to mark them, release to shoot. This matches the PS VR2
version's eye-only control scheme, which reviewers found accurate and natural. The PC version
never got it. Success means the user can play a whole area aiming only with their eyes and
prefers it to aiming with the controller.

## Goals

- Eye-gaze aiming in classic Rez (`REZ\REZ.exe`, Areas 1–5 and the rest of that program) in VR,
  with the Frame streamed from the PC through Steam Link.
- No changes to the game's files apart from adding DLLs and an ini; one step to install and one
  to remove.
- Runs beside the frame-rate mod (`rez_hfr`, `REZ\XINPUT1_3.dll`) without either getting in the
  other's way.
- Settings in an ini that can be changed while the game runs.

## Non-goals (v1)

- Area X (`AX\`, Unreal Engine 4). See open question 1.
- Running Rez on the headset itself (Proton/FEX). The Steam Link OSC stream this design uses
  exists only when streaming from a PC. See open question 4.
- Aim assist that knows enemy positions. See open question 2.
- Headsets other than the Steam Frame, and gaze sources other than Steam Link.

## Anti-goals

- Anything that makes aiming feel laggy or floaty compared with the controller. Rez locks on
  faster the faster the cursor sweeps (`docs/aim-re.md`), so heavy smoothing would make the game
  worse.
- Anything that breaks the game's own controller input, for example putting `REZ.exe` on
  SteamVR Input.
- Any interference with the user's other mods, or with the other session's frame-rate work.

## What is known (evidence in `docs/findings.md` and `docs/aim-re.md`)

- **Gaze.** Gaze reaches a program running beside Rez once two PC-side settings are on
  (SteamVR > Settings > Steam Link): "Share eye tracking data to other apps on this PC" and
  "Enable OSC" (out port 9000). Measured beside `REZ.exe`:
  - an OpenVR overlay helper, ~186 samples/s;
  - Steam Link's OSC stream (`/tracking/eye/CenterVecFull`, head-space gaze vector, +Z forward),
    ~57 samples/s.

  Neither needs input focus. Off-centre angle was 14–15° median and 35° maximum.
- **Pose source.** `REZ.exe` reads poses only from `IVRCompositor_015::WaitGetPoses`, once per
  drawn frame, and uses only `mDeviceToAbsoluteTracking`.
- **How it aims.** It aims along the selected hand controller's −Z axis. In VR control type
  "motion controller only" (cfg dword `0x98` = 1), the cursor angle is the controller's
  direction relative to the head. So setting the controller's rotation to `R_head · R_gaze`
  puts the cursor exactly where the eyes look. The cursor and lock-on run in the 60 Hz tick.
- **Oculus tilt.** If the HMD's tracking system is `"oculus"`, the game tilts aim −39.4° about
  the controller's X. SteamVR presents the Frame to this legacy app as a simulated Rift, so
  this may apply; the spike logs it.
- **Mode drift.** In the user's current mode, "head movement or motion controller" (type 0),
  gaze past ±30° permanently shifts a re-centre offset. So that mode is unusable for gaze.
- **Input-device gate.** Aiming switches to the VR path only after a VR controller button press.
  Gamepad, keyboard or mouse input switches it back.

## Design

### Shape

One DLL: a stand-in `REZ\openvr_api.dll`. The game's own DLL is renamed `openvr_api_real.dll`.

- It intercepts `VR_GetGenericInterface`. For `IVRSystem_015` and `IVRCompositor_015` it
  patches the vtable slots it needs, in place (`WaitGetPoses`: compositor slot 2). Every other
  export is a stub that calls the real DLL through `GetProcAddress`. **(my call)**
- It does not use linker forwarders. On the user's case-sensitive `S:` share, the loader asked
  for `openvr_api_real.DLL` (uppercase), and the other session had to add a copy to make Rez
  start.

### Gaze source: Steam Link's OSC stream, read inside the DLL (my call)

A thread in the DLL listens on UDP `127.0.0.1:9000` and keeps the latest gaze direction and its
time.

Why OSC rather than the OpenVR route:

- The OpenVR route can't run inside `REZ.exe`. Setting an action manifest switches the app from
  legacy input to SteamVR Input, and Rez's controls depend on legacy input.
- So the OpenVR route would need a second process plus shared memory, which is one more thing to
  start and keep running.
- 57 samples/s is close to the game's 60 Hz cursor tick.

The fallback is a small helper exe using the OpenVR overlay route, feeding the DLL through
shared memory. **v1 builds only the `GazeSource` seam, not the helper (my call).**

The helper gets built if either of these holds:

- on the headset, OSC's median latency trails the OpenVR route's by more than 17 ms (one game
  tick), measured by logging both on the same eye movements;
- the user finds the aim laggy, and the filter isn't the cause.

At 57 samples/s against the 60 Hz tick, about one tick in 20 repeats the previous sample. At
fixation that's invisible, and in a saccade it adds at most one tick. Acceptable.

If no OSC arrives for 250 ms, aiming falls back to head-forward. The log says why: the settings
are off, or the port is taken (VRChat and similar OSC tools use 9000; the port is configurable,
and Steam Link offers 9015).

### Aim

1. On each `WaitGetPoses`, take the HMD pose (device 0) from the same array.
2. Turn the head-space gaze into world space: `d = R_head · g`.
3. For both hand controllers, replace the rotation of `mDeviceToAbsoluteTracking` with a frame
   whose −Z is `d` and whose roll follows the head's up. Keep the position.
4. If the tracking system is `"oculus"`, post-multiply by `Rx(+39.4°)` to cancel the game's
   tilt.

Leave the HMD entry, validity flags and velocities alone.

- **Both hands are rewritten (my call).** The game aims with whichever controller was last
  "selected" by a button press, so rewriting both means it doesn't matter which one that was.
- **Real controller aim is lost while the mod is active.** That is the point of the mod; see open
  question 3 for a way to switch back mid-game.
- **Menus.** The menu pointer also follows the controller, so menus follow gaze too. That's the
  default **(my call)**; an ini switch keeps menus on head or controller if it's annoying.

### First step: prove the approach on the headset before building v1

As of this draft the spike has never run in VR, so nothing here has been seen on the Frame:
the tracking system, the hand devices' class and pose validity, the input-device gate, and
whether rewriting the pose moves the cursor. The plan starts with a headset session, run by
Claude with the user wearing the headset. Launches are announced to the frame-rate session
first, and never made while another Rez is running.

1. **`mode=log`.** Record from the log:
   - the HMD tracking system (`"oculus"` or not);
   - the hand indices and their classes;
   - control type, input device and selected hand, while the user presses each Frame
     controller button.
2. **`mode=head`.** With the control type at "motion controller only", the cursor should follow
   the head with the controller ignored.

Go/no-go: if hand poses aren't valid Controller-class devices, or the input-device value never
reaches 3 from the user's controls, stop and revise the design. That revision would mean a
`GetControllerState` hook that injects a selecting press, or holding the input-device value
at 3. Only after both steps pass are the filter, installer and tests built.

### Required game setting

VR control type must be "motion controller only".

- The DLL logs the current type every 5 s (read-only, at the analysed addresses, and only when
  the exe's PE TimeDateStamp is `0x658122f1`).
- The DLL writes nothing to the game. The user sets the mode once in the game's options
  **(my call)**.
- The install step tells the user to set it.

### Feel

- **Fixation filter.** A One Euro filter on the gaze direction, which smooths heavily when still
  and lightly when moving fast.
  - Default parameters are chosen so that saccades pass through within about one frame.
  - Tunable in the ini; filtering can be turned off.
  - Starting point **(my call)**: min cutoff 1.5 Hz, beta 0.02; tune on the headset.
- **Blinks.** OSC also sends eyelid values (`/tracking/eye/EyesClosedAmount`,
  `/sl/xrfb/facew/EyesClosedL|R`), but their meaning isn't settled: one sample had the left
  eye "closed 1.0" with the left lid "1.0", and the right eye at 0.27.
  - Plan: one headset logging run with deliberate blinks, then decide.
  - Fallback if the lid data is useless: hold the last direction across implausible jumps.
- **Cursor rate.** The game moves the cursor at 60 Hz whatever the headset rate. Nothing to do.
- **Calibration offset.** The ini has yaw/pitch trim in case gaze and cursor disagree by a
  constant amount.

### Settings (`REZ\rez_gaze.ini`, reread every second)

| Key | Values | Default |
|---|---|---|
| `enabled` | 0/1 | 1 |
| `menus` | gaze/head/controller | gaze |
| `filter_min_cutoff` | Hz | 1.5 |
| `filter_beta` | number | 0.02 |
| `trim_yaw`, `trim_pitch` | degrees | 0 |
| `tilt` | auto/degrees | auto |
| `osc_port` | port | 9000 |
| `log` | 0/1 | 1 |

### Install and remove

`tools\rez_gaze_install.py` (or a `.cmd`) takes the Rez install path. It:

1. checks no `REZ.exe` is running;
2. renames `openvr_api.dll` → `openvr_api_real.dll`, unless that's already done;
3. copies the DLL and writes a default ini;
4. prints the two Steam Link settings and the in-game VR control type to set.

Remove reverses it, and leaves `openvr_api_real.DLL` if it was another tool's.

Details **(my call)**:

- The installer identifies Rez's original DLL by SHA-256: the 302,880-byte file from 2021.
  It never overwrites a file it can't identify.
- The uppercase `openvr_api_real.DLL` copy belongs to the other session. Leave it.
- Since v1 has no forwarders, the copy is no longer needed: v1 loads `openvr_api_real.dll` by
  its exact name.
- Targets: `REZ.exe` and the frame-rate session's `REZ_120.exe`. They have the same PE
  TimeDateStamp and the same layout, so the state logging works for both. Only the tick rate
  differs, and nothing in this design depends on it.
- Ownership of `openvr_api.dll` long term is being agreed with the frame-rate session, whose
  notes see it as a way in for VR interpolation. If it needs OpenVR hooks too, v1 adds a small
  exported callback registry (pre/post `WaitGetPoses` and `Submit`) rather than two mods
  patching the same objects blindly.

### Coexisting with rez_hfr

`rez_hfr` hooks XInput and the game's camera code; this mod hooks OpenVR poses. Neither touches
the other's DLL or files.

- The frame-rate mod draws extra frames, and `WaitGetPoses` runs per drawn frame. The cursor
  still uses the latest gaze at the 60 Hz tick.
- If the frame-rate mod later interpolates the camera between ticks, the gaze cursor isn't
  affected: it is computed from angles relative to the head.

## Testing

- **Without the headset.**
  - Unit tests for the OSC parser, the One Euro filter and the aim matrix. The matrix tests
    check that a gaze of (yaw, pitch) produces the game's own `atan2` angles under the
    type-2 formula in `docs/aim-re.md`, with and without the oculus tilt.
  - `gaze_probe replay` feeds recorded or synthetic gaze into the DLL's OSC port.
- **With the headset**, following a written checklist:
  1. `mode=log`: read the logged tracking system, hand indices and classes, control type and
     input device.
  2. `mode=head`: the cursor should track the head with the controller ignored. This proves
     the pose rewrite.
  3. `mode=gaze`: hit-rate and feel over one area; compare against controller aim.
  4. Blink logging run.
  5. Latency: gaze step → cursor move, filmed or logged.
- **Coordination.** Message the frame-rate session before any launch, and never launch while
  another Rez is running.

## Open questions for the user

0. **What do you fire with?** The Frame controllers' triggers, or a gamepad?
   - The game aims through the VR path only while its last input came from a VR controller.
     Any gamepad, keyboard or mouse input switches it back to stick aim.
   - If you use a gamepad, or Steam Input also presents the Frame controllers as an XInput
     pad, the mod has to hold the game on the VR path. That is a write to game memory, or
     injected presses, which this draft otherwise avoids.
   - The headset step above shows which happens; your answer says what to aim for.
1. **Area X.** Include it in v1? It uses the same vtable slots (`IVRSystem_012`,
   `IVRCompositor_016`), but its aiming code (UE4) hasn't been analysed, and it loads its DLL from
   `AX\Engine\Binaries\ThirdParty\OpenVR\OpenVRv1_0_2\Win64\`.
2. **Aim assist.** Is snapping to the nearest enemy wanted? The lock box is cursor ± 35 px, which
   may already absorb tracker error. Real snapping means finding enemy positions in the game's
   memory: a separate, larger reverse-engineering job.
3. **Switching back mid-game.** Should a controller gesture (e.g. hold grip) temporarily return
   aiming to the real controller, or is the ini switch enough?
4. **Running on the headset.** Is Proton/FEX standalone play wanted later? If so, the gaze source
   there would have to be different (on-device SteamVR has no Steam Link OSC), which affects
   whether the `GazeSource` seam should be built in now.

## Left to the builder

- Code structure, test framework, and the exact One Euro defaults after tuning.
- Whether the stubs are generated or hand-written.
- Log format.
