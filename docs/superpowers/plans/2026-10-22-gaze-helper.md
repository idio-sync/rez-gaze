# Gaze Helper Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `rez_gaze_helper.exe`, an OpenVR overlay app that reads the Frame's gaze and publishes
it, head-relative, to shared memory. The stand-in `openvr_api.dll` starts it and aims with it,
falling back to OSC and then to head-forward.

**Architecture:** A shared header defines the shared-memory block and a seqlock writer and
reader. The helper is a separate exe in `REZ\rez_gaze\`, with its own OpenVR 2.15.6 runtime. The
DLL starts it on the first `IVRSystem_015` request and picks a gaze source each frame.
Pure logic (seqlock, aim math, source choice) gets unit tests in one test exe under CTest.

**Tech Stack:** C++20, MSVC 14.50 via `build.cmd` (VS 18 Build Tools + `C:\ninja.exe`),
OpenVR SDK 2.15.6 (`third_party/openvr`), Win32 file mappings, CTest.

**Spec:** `docs/superpowers/specs/2026-10-22-rez-gaze-aim-design.md` (section "Gaze source")

## Global Constraints

- **Mapping name:** `Local\rez_gaze_<game pid>`, as a decimal PID.
- **Coordinates:** gaze in the block is head-space, −Z forward, +X right, +Y up, unit length.
- **Fresh means published within 250 ms.** Older samples count as no gaze.
- **Source order for `source=auto`:** helper, then OSC, then head-forward.
  `source=helper|osc` forces one.
- **Spawn rules:**
  - Start the helper once per game process, on the first non-null `IVRSystem_015`.
  - A flat run never starts it.
  - It's launched with `CREATE_NO_WINDOW`.
- **The helper lives in `REZ\rez_gaze\`, with its own `openvr_api.dll` (SDK 2.15.6) beside it.**
  - In `REZ\` it would load the stand-in DLL, which forwards to Rez's 2021 DLL. That DLL has
    no `VR_InitInternal2`, which SDK 2.15.6's `VR_Init` needs.
  - It writes its action manifest and bindings into `REZ\rez_gaze\openvr_input\`.
  - Binding path `/user/head/eyetracking`, controller types `frame_hmd` and `remote_hmd` (as
    the probe does).
- **The helper never starts SteamVR on its own initiative.** The DLL starts it only after the
  game has initialised VR. The default app type is `overlay` (the one proven beside Rez), and
  `--app background` is for tests.
- **The helper exits within 1 s** of the game process exiting, or on `VREvent_Quit`.
- **Its log is `REZ\rez_gaze\rez_gaze_helper.log`.**
- **The DLL writes nothing to game memory.**
- **Builds go only into `out\`.** The frame-rate session builds in its own worktree.

## Review Focus

1. **The helper is missing or won't start** (file absent, SteamVR refuses). The game must keep
   running, with OSC or head-forward aim, and the log must name the reason. Test:
   `ChooseSource` cases in Task 4, plus the harness run with the helper renamed, in Task 5.
2. **The game exits or crashes.** The helper must not linger. Test: Task 3's smoke run kills its
   parent and checks the helper's exit within 1 s.
3. **A stale block from an earlier run** (a reused PID, or a hung helper). Old samples must not
   steer the aim. Test: `Read` reports age, and `ChooseSource` rejects an age over 250 ms
   (Tasks 1 and 4).
4. **Torn reads** while the helper writes at about 250 Hz and the game reads at 144 Hz. The
   reader must never return a half-written sample. Test: the two-thread hammer test in Task 1.
5. **`IVRSystem_015` is requested more than once** (Rez re-fetches interfaces when the init
   token changes). Only one helper may be started. Test: Task 5's harness asks twice and counts
   helper processes.

---

### Task 1: Shared-memory block and seqlock

**Files:**
- Create: `shared/gaze_shm.h`
- Create: `tests/test_main.cpp`, `tests/test_gaze_shm.cpp`
- Modify: `CMakeLists.txt` (add `enable_testing()`, the `rez_gaze_tests` exe and
  `add_test(NAME unit COMMAND rez_gaze_tests)`)

**Interfaces:**
- Produces, all header-only in namespace `rezgaze`:
  ```cpp
  constexpr uint32_t kShmMagic = 0x5A47525A;  // "ZRGZ"
  constexpr uint32_t kShmVersion = 1;
  struct GazeSampleShm { float dir[3]; uint32_t flags; int64_t qpc; uint64_t count; };  // flags: 1 active, 2 valid, 4 tracked
  struct GazeShmBlock { uint32_t magic, version; volatile LONG seq; uint32_t helper_pid; GazeSampleShm sample; char status[128]; };
  std::wstring ShmName(DWORD game_pid);                 // L"Local\\rez_gaze_<pid>"
  class GazeShmWriter { public: bool Create(const std::wstring& name); void Publish(const GazeSampleShm&); void SetStatus(const char*); ~GazeShmWriter(); };
  class GazeShmReader { public: bool Open(const std::wstring& name); bool Read(GazeSampleShm& out, std::string* status) const; ~GazeShmReader(); };
  ```
  `Read` returns false if the block isn't open, or the magic or version is wrong, or no
  consistent snapshot was obtained in 8 attempts.
- The tests use a tiny assert framework in `tests/test_main.cpp`: a `TEST(name)` macro, an
  `EXPECT(cond)` that records failures, and `main` running all tests and returning nonzero on
  failure. No external dependency.

- [ ] **Step 1: Write the failing tests** in `tests/test_gaze_shm.cpp`:
  - `shm_roundtrip`: Create, Publish {dir 0.1,0.2,−0.97, flags 7, qpc 123, count 5}, Open, Read. The fields come back equal.
  - `shm_status`: `SetStatus("vr init failed")`; `Read` returns that status text.
  - `shm_reader_before_writer`: `Open` on an unused name returns false.
  - `shm_bad_magic`: write magic 0 through a raw mapping view; `Read` returns false.
  - `shm_odd_seq_is_torn`: force `seq` odd through a raw view; `Read` returns false.
  - `shm_hammer`: a writer thread publishes 200,000 samples with
    `dir = {i, i, i}` and `count = i`, while the main thread reads in a loop. Every successful
    read has `dir[0] == dir[1] == dir[2] == count`.
- [ ] **Step 2: Run it to make sure it fails.** Run `build.cmd`; it must fail to compile (`gaze_shm.h` missing).
- [ ] **Step 3: Implement `shared/gaze_shm.h`.**
  - The writer uses `CreateFileMappingW(INVALID_HANDLE_VALUE, …, PAGE_READWRITE, sizeof(GazeShmBlock))`.
  - `Publish` increments `seq` to odd (`InterlockedIncrement`), copies the sample, increments it to even.
  - The reader reads `seq`; retries if it's odd; copies; re-reads `seq`; accepts the copy if unchanged.
  - Use `MemoryBarrier()` around the copy.
- [ ] **Step 4: Run the tests.** `cmd //c build.cmd` then `ctest --test-dir out --output-on-failure`. Expected: `100% tests passed`.
- [ ] **Step 5: Commit** `shared/ tests/ CMakeLists.txt`: "Shared-memory gaze block with a seqlock, and unit tests".

### Task 2: Aim math as pure functions, checked against the game's own formula

**Files:**
- Create: `proxy/aim.h`, `proxy/aim.cpp` (moved out of `proxy/proxy.cpp`: `V3`, the vector ops, `Aim`)
- Create: `tests/test_aim.cpp`
- Modify: `proxy/proxy.cpp` (use `aim.h`), `CMakeLists.txt` (add `proxy/aim.cpp` to both the
  proxy target and the tests)

**Interfaces:**
- Produces:
  ```cpp
  namespace rezgaze {
  struct V3 { double x, y, z; };   // with +, -, * scalar, Dot, Cross, Norm as in the spike
  struct Mat3 { V3 col[3]; };      // columns = the controller's x, y, z axes in world space
  Mat3 AimRotation(const Mat3& head, V3 gaze_head, double tilt_deg, double trim_yaw_deg, double trim_pitch_deg);
  // The game's type-2 cursor angles ("motion controller only"), from docs/aim-re.md: yaw/pitch in radians.
  void GameCursorAngles(const Mat3& head, const Mat3& ctrl, bool is_oculus, double& yaw, double& pitch);
  }
  ```
  `GameCursorAngles` re-implements the decompiled formula in column-vector form:
  - the controller relative to the head is `R = headᵀ · ctrl`;
  - if `is_oculus`, `R = R · Rx(−0.68766)`;
  - `f = R · (0,0,−1)`;
  - `yaw = atan2(f.x, −f.z)`, `pitch = atan2(f.y, −f.z)`.

  It exists only to test `AimRotation`.

- [ ] **Step 1: Write the failing tests** in `tests/test_aim.cpp`, over 1,000 random head
  rotations and gaze directions within 40° of centre:
  - `aim_matches_game_not_oculus`: `AimRotation(head, g, 0, 0, 0)` fed to
    `GameCursorAngles(head, ·, false)` gives `yaw = atan2(g.x, −g.z)` and
    `pitch = atan2(g.y, −g.z)`, within 1e−6 rad.
  - `aim_matches_game_oculus`: the same with `tilt_deg = 39.4` and `is_oculus = true`, within
    1e−3 rad. 39.4 is the spec's rounding of 0.68766 rad = 39.4001°.
  - `aim_trim`: with `trim_yaw_deg = 2` and gaze straight ahead under an identity head, yaw
    is 2° within 1e−6.
  - `aim_orthonormal`: each `AimRotation` result has unit columns with pairwise dots < 1e−9,
    and determinant +1.
- [ ] **Step 2: Run** `build.cmd`: it fails to compile (`aim.h` missing).
- [ ] **Step 3: Implement `aim.cpp`** by moving the spike's `Aim` and vector code. The spike
  applies yaw about local y, then pitch about local x, with tilt added to pitch; keep that
  order. `proxy.cpp` converts `HmdMatrix34_t` to and from `Mat3` at the call site.
- [ ] **Step 4: Run** `ctest --test-dir out --output-on-failure`. Expected: all pass. Rebuild the
  proxy (it must still link).
- [ ] **Step 5: Commit**: "Aim math as tested pure functions, checked against REZ.exe's cursor formula".

### Task 3: `rez_gaze_helper.exe`

**Files:**
- Create: `helper/gaze_helper.cpp`
- Modify: `CMakeLists.txt` (`rez_gaze_helper` exe, output to `out/rez_gaze/`, linking
  `openvr_api.lib` 2.15.6; post-build copy of `openvr_api.dll` 2.15.6 into `out/rez_gaze/`)
- Modify: `probe/main.cpp` and a new `probe/shm_probe.cpp`: the mode `gaze_probe shm --pid N`
  prints the block once a second (status, flags, sample rate from `count`, age)

**Interfaces:**
- Consumes: Task 1's `GazeShmWriter`, `ShmName`.
- Produces the command line
  `rez_gaze_helper.exe --parent <pid> [--app overlay|background] [--hz 250]`.
  It publishes to `ShmName(parent)` and sets the status text to one of:
  - `"starting"`;
  - `"vr init failed: <english description>"`;
  - `"manifest failed: <error>"`;
  - `"running"`;
  - `"parent exited"`;
  - `"steamvr quit"`.

  Exit codes: 0 for a normal exit, 2 for bad arguments, 3 when VR init failed.

- [ ] **Step 1: Write the failing smoke check** as a CTest script test
  `tests/helper_smoke.cmd`, with SteamVR possibly not running:
  1. Start `timeout /t 3` as a parent and note its PID.
  2. Run the helper with `--parent <that pid> --app background`.
  3. Check that the helper exits within 4 s.
  4. Check that its log has a status line: `"vr init failed"` if SteamVR is off, otherwise
     `"running"` then `"parent exited"`.

  `add_test(NAME helper_smoke …)`.
- [ ] **Step 2: Run** `ctest -R helper_smoke`: it fails, because there's no helper exe.
- [ ] **Step 3: Implement the helper.** Reuse the probe's manifest-writing approach, but write
  into `<exe dir>\openvr_input\`. Then the loop:
  1. `UpdateActionState`.
  2. `GetEyeTrackingDataRelativeToNow(TrackingUniverseStanding, 0)` and
     `GetDeviceToAbsoluteTrackingPose(Standing, 0)` for the HMD.
  3. Head-space direction `= headRᵀ · normalize(target − origin)`. Flags come from
     bActive/bValid/bTracked. `qpc = QueryPerformanceCounter`.
  4. Publish.
  5. Drain events; on `VREvent_Quit`, `AcknowledgeQuit_Exiting` and exit.
  6. Check the parent with `WaitForSingleObject(parent, 0)`.
  7. Sleep `1000/hz` ms (`timeBeginPeriod(1)`).

  Write a log line at start, at each status change, and every 5 s: samples, tracked %.
- [ ] **Step 4: Run** `ctest --test-dir out --output-on-failure`. Expected: `helper_smoke`
  passes. Also run once with SteamVR running and the headset off, by hand:
  `gaze_probe shm --pid <parent>` shows status `running`, a count rising at about 250/s, and
  flags without "tracked".
- [ ] **Step 5: Commit**: "rez_gaze_helper: gaze from OpenVR's eyetracking action into shared memory".

### Task 4: Gaze sources in the DLL, and starting the helper

**Files:**
- Create: `proxy/gaze_source.h`, `proxy/gaze_source.cpp` (the OSC listener moved from
  `proxy.cpp`, plus the helper source and `ChooseSource`)
- Create: `tests/test_gaze_source.cpp`
- Modify: `proxy/proxy.cpp` (config key `source`; spawning on the first non-null
  `IVRSystem_015`; `WorldGaze` takes the chosen reading), `CMakeLists.txt`

**Interfaces:**
- Consumes: Task 1's `GazeShmReader`, `ShmName`.
- Produces:
  ```cpp
  namespace rezgaze {
  enum class SourceMode { Auto, Helper, Osc };
  enum class Source { Helper, Osc, Head };
  // Ages in ms, or < 0 for "never". A helper sample counts only if tracked.
  Source ChooseSource(SourceMode mode, double helper_age_ms, bool helper_tracked, double osc_age_ms);
  struct GazeReading { V3 dir_head; Source source; };
  class HelperSource { public: bool Start(const std::wstring& helper_exe, DWORD game_pid); bool Read(V3& dir, double& age_ms, bool& tracked, std::string& status); };
  class OscSource { public: void Start(int port); bool Read(V3& dir, double& age_ms); };
  }
  ```
  `HelperSource::Start` runs `CreateProcessW(<dll dir>\rez_gaze\rez_gaze_helper.exe, "--parent <pid>", CREATE_NO_WINDOW)`
  at most once per process, guarded by a `std::once_flag`. It opens the mapping lazily on each
  `Read` until that succeeds.

- [ ] **Step 1: Write the failing tests** in `tests/test_gaze_source.cpp`, for `ChooseSource`:
  - (Auto, 10, true, 10) → Helper
  - (Auto, 300, true, 10) → Osc
  - (Auto, 10, false, 10) → Osc
  - (Auto, −1, false, −1) → Head
  - (Auto, 300, true, 300) → Head
  - (Helper, 300, true, 10) → Head
  - (Osc, 10, true, 10) → Osc
  - (Osc, 10, true, 260) → Head
- [ ] **Step 2: Run**: compile failure.
- [ ] **Step 3: Implement**, with these behaviours:
  - The DLL logs a line whenever the chosen source changes, with the helper status text when
    it isn't "running".
  - The 5 s report names the source and both ages.
  - When the helper is chosen, `WorldGaze` uses its direction; the smoothing stays as is.
- [ ] **Step 4: Run** the tests (all pass) and build the proxy.
- [ ] **Step 5: Commit**: "DLL: start the helper, pick helper/OSC/head gaze per frame".

### Task 5: Harness: the whole path without the game

**Files:**
- Create: `tests/proxy_harness.cpp`, the exe `proxy_harness` (output `out/harness/`)
- Modify: `CMakeLists.txt`. A post-build step lays out `out/harness/`:
  - the stand-in as `openvr_api.dll`;
  - SDK 2.15.6's `openvr_api.dll` copied as `openvr_api_real.dll`;
  - `rez_gaze/` with the helper, its runtime, and a test `rez_gaze.ini` with `mode=log`.

**Interfaces:**
- Consumes: the stand-in's exports. Task 3's helper and Task 4's spawn rule.

- [ ] **Step 1: Write the harness.**
  1. It calls `VR_InitInternal(&err, VRApplication_Background)` through the stand-in. If
     SteamVR isn't running, it prints `SKIP: SteamVR not running` and exits 0.
  2. It calls `VR_GetGenericInterface("IVRSystem_015")` twice, waits 2 s, and counts
     `rez_gaze_helper.exe` processes whose parent PID is its own (Toolhelp snapshot): it must
     be exactly 1.
  3. It opens `ShmName(own pid)` and checks the status is "running".
  4. It calls `VR_ShutdownInternal`, exits, and a wrapper `tests/harness.cmd` checks that the
     helper is gone 1 s later.
- [ ] **Step 2: Run** `tests\harness.cmd`, with SteamVR running and the headset off. Expected:
  `PASS: 1 helper, status running, helper exited`. If SteamVR is off: `SKIP`.
- [ ] **Step 3: Run** it once with `out/harness/rez_gaze/rez_gaze_helper.exe` renamed. Expected:
  the stand-in's log says the helper failed to start (with the Win32 error), and the harness
  still exits 0.
- [ ] **Step 4: Commit**: "Harness: stand-in DLL starts one helper and it exits with the host".

### Task 6: Install the new layout into REZ\ (no launch)

- [ ] **Step 1:** Check that no `REZ.exe`/`REZ_120.exe`/`Rez-infinite.exe` is running (if one is,
  stop and wait). Copy into `S:\_Steam Library\steamapps\common\Rez Infinite\REZ\`:
  - the stand-in as `openvr_api.dll`;
  - the `rez_gaze\` folder: the helper, the 2.15.6 `openvr_api.dll`;
  - `rez_gaze.ini` with `mode=log` and `source=auto`.

  Leave `openvr_api_real.dll`, `openvr_api_real.DLL` and the frame-rate session's files alone.
- [ ] **Step 2:** Update `docs/findings.md` with the helper milestone results, and the README's
  test-run section with the new headset checklist (log → head → gaze, then `source=helper` vs
  `source=osc`). Commit: "Docs: helper milestone, headset checklist".
