// Spike: a stand-in openvr_api.dll for REZ.exe that aims the controller with the eyes.
//
// REZ.exe asks for IVRSystem_015 (SDK 1.0.5 layout) and IVRCompositor_015 (SDK 1.0.1 layout).
// We patch the pose-returning slots of those vtables in place and rewrite the aiming
// controller's pose before the game sees it. Gaze comes from Steam Link's OSC stream
// (/tracking/eye/CenterVecFull on 127.0.0.1:9000), read on a thread in this DLL.
//
// How REZ.exe aims (docs/aim-re.md): it reads poses only from WaitGetPoses, and aims along the
// selected hand controller's -Z. With "VR control type" = "motion controller only" the cursor
// angle is the controller's direction relative to the head, so a controller rotation of
// R_head * R_gaze puts the cursor where the eyes look. If the tracking system is "oculus"
// (SteamVR may simulate a Rift for this legacy app), the game tilts the aim by -39.4 deg about
// the controller's X, which tilt=auto undoes.
//
// rez_gaze.ini beside the DLL, reread every second:
//   [rez_gaze]
//   mode=log|head|gaze|calibrate  log: record calls only; head: controllers point where the
//                          head points; gaze: controllers point where the eyes look;
//                          calibrate: the cursor is pinned in the world, the user keeps their
//                          eyes on it and moves the head; after calib_seconds the fit is saved
//                          as calib= and the mode returns to gaze (trims reset)
//   calib=m00,...,m12      angle-space correction from mode=calibrate (written by it)
//   calib_seconds=15       how long calibration collects before fitting
//   calib_combo=both|one   start (or cancel) calibration from the controllers: hold both
//                          thumbsticks down (both), or either one (one), for calib_hold_s
//   calib_hold_s=2         (3 by default with calib_combo=one); 0 turns the combination off.
//                          Buzzes: short = started, double = saved, long = cancelled
//   hand=right|left|both   which controller(s) to rewrite
//   tilt=auto              degrees about the controller's X added to undo the game's offset;
//                          auto = 39.4 when the tracking system is "oculus", else 0
//   yaw=0, pitch=0         trim in head space, degrees (positive = right, up)
//   smooth_ms=0            exponential smoothing of the gaze direction (filter=ema)
//   filter=ema|euro        euro: One Euro filter instead, steady at rest, quick in saccades
//   euro_min_cutoff=1.0, euro_beta=0.05   its cut-off (Hz) at rest, and speed gain
//   gain_yaw=1, gain_pitch=1  multiply the gaze angles (cursor not reaching the edges)
//   helper=1               start the helper (rez_gaze folder)
//   helper_delay_s=10      seconds after the first WaitGetPoses before starting it (an overlay
//                          app joining during Rez's VR start-up left Rez black and silent)
//   source=auto|helper|osc gaze from the helper (rez_gaze\rez_gaze_helper.exe, started on the
//                          first IVRSystem_015) if fresh, else Steam Link OSC, else straight
//                          ahead; helper/osc forces one
//   port=9000              OSC port (read at start only)
// Log: rez_gaze.log beside the DLL.
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <openvr.h>

#include "aim.h"
#include "calib.h"
#include "combo.h"
#include "filter.h"
#include "gaze_source.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

// ---- config and log ------------------------------------------------------------------------

std::wstring g_dir;  // folder of this DLL, with trailing backslash
FILE* g_log = nullptr;
std::mutex g_log_mutex;

void Log(const char* fmt, ...) {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    if (!g_log) return;
    SYSTEMTIME st;
    GetLocalTime(&st);
    std::fprintf(g_log, "%02d:%02d:%02d.%03d ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    va_list ap;
    va_start(ap, fmt);
    std::vfprintf(g_log, fmt, ap);
    va_end(ap);
    std::fputc('\n', g_log);
    std::fflush(g_log);
}

enum class Mode { Log, Head, Gaze, Calibrate };

struct Config {
    Mode mode = Mode::Log;
    bool right = true, left = true;
    bool tilt_auto = true;
    double tilt = 0, pitch = 0, yaw = 0, smooth_ms = 0;
    double gain_yaw = 1, gain_pitch = 1;
    bool filter_euro = false;
    rezgaze::Affine2 calib;  // identity unless rez_gaze.ini has calib=
    double calib_seconds = 15;
    bool calib_combo_one = false;
    double calib_hold_s = 2;
    double euro_min_cutoff = 1.0, euro_beta = 0.05;
    bool helper = true;
    double helper_delay_s = 10;
    int port = 9000;
    rezgaze::SourceMode source = rezgaze::SourceMode::Auto;
};

Config g_cfg;
std::mutex g_cfg_mutex;

Config ReadConfig() {
    std::wstring ini = g_dir + L"rez_gaze.ini";
    wchar_t buf[64];
    Config c;
    GetPrivateProfileStringW(L"rez_gaze", L"mode", L"log", buf, 64, ini.c_str());
    c.mode = _wcsicmp(buf, L"head") == 0        ? Mode::Head
             : _wcsicmp(buf, L"gaze") == 0      ? Mode::Gaze
             : _wcsicmp(buf, L"calibrate") == 0 ? Mode::Calibrate
                                                : Mode::Log;
    GetPrivateProfileStringW(L"rez_gaze", L"hand", L"both", buf, 64, ini.c_str());
    c.right = _wcsicmp(buf, L"left") != 0;
    c.left = _wcsicmp(buf, L"right") != 0;
    auto num = [&](const wchar_t* key, double def) {
        wchar_t b[64];
        GetPrivateProfileStringW(L"rez_gaze", key, L"", b, 64, ini.c_str());
        return b[0] ? _wtof(b) : def;
    };
    GetPrivateProfileStringW(L"rez_gaze", L"tilt", L"auto", buf, 64, ini.c_str());
    c.tilt_auto = _wcsicmp(buf, L"auto") == 0;
    c.tilt = c.tilt_auto ? 0 : _wtof(buf);
    c.pitch = num(L"pitch", 0);
    c.yaw = num(L"yaw", 0);
    c.smooth_ms = num(L"smooth_ms", 0);
    c.gain_yaw = num(L"gain_yaw", 1);
    GetPrivateProfileStringW(L"rez_gaze", L"filter", L"ema", buf, 64, ini.c_str());
    c.filter_euro = _wcsicmp(buf, L"euro") == 0;
    {
        wchar_t cal[256];
        GetPrivateProfileStringW(L"rez_gaze", L"calib", L"", cal, 256, ini.c_str());
        std::string text;
        for (const wchar_t* p = cal; *p; ++p) text += static_cast<char>(*p);  // digits, commas, signs
        rezgaze::ParseCalibration(text, c.calib);  // stays identity if absent or malformed
    }
    c.calib_seconds = num(L"calib_seconds", 15);
    GetPrivateProfileStringW(L"rez_gaze", L"calib_combo", L"both", buf, 64, ini.c_str());
    c.calib_combo_one = _wcsicmp(buf, L"one") == 0;
    c.calib_hold_s = num(L"calib_hold_s", c.calib_combo_one ? 3 : 2);
    c.euro_min_cutoff = num(L"euro_min_cutoff", 1.0);
    c.euro_beta = num(L"euro_beta", 0.05);
    c.gain_pitch = num(L"gain_pitch", 1);
    c.helper = num(L"helper", 1) != 0;
    c.helper_delay_s = num(L"helper_delay_s", 10);
    c.port = static_cast<int>(num(L"port", 9000));
    GetPrivateProfileStringW(L"rez_gaze", L"source", L"auto", buf, 64, ini.c_str());
    c.source = _wcsicmp(buf, L"helper") == 0 ? rezgaze::SourceMode::Helper
               : _wcsicmp(buf, L"osc") == 0  ? rezgaze::SourceMode::Osc
                                             : rezgaze::SourceMode::Auto;
    return c;
}

const char* ModeName(Mode m) {
    return m == Mode::Head ? "head" : m == Mode::Gaze ? "gaze" : m == Mode::Calibrate ? "calibrate" : "log";
}

// ---- gaze sources -------------------------------------------------------------------------

using rezgaze::Mat3;
using rezgaze::Norm;
using rezgaze::V3;

LONGLONG g_qpf = 1;

LONGLONG Qpc() {
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return c.QuadPart;
}

rezgaze::HelperSource g_helper;
rezgaze::OscSource g_osc;
rezgaze::Source g_source = rezgaze::Source::Head;  // the last one used
std::string g_helper_status;

// ---- pose rewriting ------------------------------------------------------------------------

vr::IVRSystem* g_system = nullptr;  // the game's IVRSystem_015 object

using FnRoleToIndex = vr::TrackedDeviceIndex_t(__fastcall*)(void*, vr::ETrackedControllerRole);
FnRoleToIndex g_orig_role_to_index = nullptr;

vr::TrackedDeviceIndex_t g_right = vr::k_unTrackedDeviceIndexInvalid;
vr::TrackedDeviceIndex_t g_left = vr::k_unTrackedDeviceIndexInvalid;
V3 g_smoothed{0, 0, -1};
rezgaze::OneEuroGaze g_euro(1.0, 0.05);
LONGLONG g_smoothed_qpc = 0;

// ---- game state, read-only (addresses from docs/aim-re.md, REZ.exe 2.8 only) --------------

std::atomic<bool> g_is_oculus{false};  // TrackingSystemName of the HMD == "oculus"

constexpr DWORD kRezTimeDateStamp = 0x658122f1;
uintptr_t g_rez_base = 0;  // 0 when the exe isn't the analysed build

void FindRez() {
    auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->FileHeader.TimeDateStamp == kRezTimeDateStamp) g_rez_base = base;
    Log("exe TimeDateStamp 0x%08lx: %s", nt->FileHeader.TimeDateStamp,
        g_rez_base ? "REZ.exe 2.8, state logging on" : "not the analysed build, state logging off");
}

struct RezState {
    int control_type = -1;  // byte 0x14052265c: 0 head+controller, 1 controller only, 2 head only
    int input_device = -1;  // DAT_1404cc8a8: 3 = VR controller
    int selected = -1;      // manager +0xa08: 0 left, 1 right
    int is_oculus = -1;     // manager +0xc58
    float drift = 0;        // manager +0xc70: type-1 yaw offset (rad)
    float cursor_yaw = 0;   // DAT_1409ac298: the aim yaw the game computed this frame (rad)
    float cursor_pitch = 0; // DAT_1409ac18c: the aim pitch (rad)
};

bool ReadRezState(RezState& s) {
    if (!g_rez_base) return false;
    __try {
        s.control_type = *reinterpret_cast<unsigned char*>(g_rez_base + 0x52265c);
        s.input_device = *reinterpret_cast<int*>(g_rez_base + 0x4cc8a8);
        s.cursor_yaw = *reinterpret_cast<float*>(g_rez_base + 0x9ac298);
        s.cursor_pitch = *reinterpret_cast<float*>(g_rez_base + 0x9ac18c);
        auto mgr = *reinterpret_cast<uintptr_t*>(g_rez_base + 0xa1e360);
        if (mgr) {
            s.selected = *reinterpret_cast<int*>(mgr + 0xa08);
            s.is_oculus = *reinterpret_cast<unsigned char*>(mgr + 0xc58);
            s.drift = *reinterpret_cast<float*>(mgr + 0xc70);
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Unpatched IVRSystem_015 slots, called through the game's own object.
using FnClass = vr::ETrackedDeviceClass(__fastcall*)(void*, vr::TrackedDeviceIndex_t);
using FnStrProp = uint32_t(__fastcall*)(void*, vr::TrackedDeviceIndex_t, vr::ETrackedDeviceProperty, char*, uint32_t,
                                        vr::ETrackedPropertyError*);

std::string DeviceString(vr::TrackedDeviceIndex_t i, vr::ETrackedDeviceProperty p) {
    char buf[128] = {};
    vr::ETrackedPropertyError e;
    reinterpret_cast<FnStrProp>((*reinterpret_cast<void***>(g_system))[26])(g_system, i, p, buf, sizeof buf, &e);
    return buf;
}

int DeviceClass(vr::TrackedDeviceIndex_t i) {
    if (i == vr::k_unTrackedDeviceIndexInvalid) return -1;
    return reinterpret_cast<FnClass>((*reinterpret_cast<void***>(g_system))[19])(g_system, i);
}

// The helper's status changes, and its exit code once if it has died, in every mode.
void LogHelperState() {
    V3 d;
    double age;
    bool tracked;
    std::string status;
    if (g_helper.Read(d, age, tracked, status) && status != g_helper_status) {
        Log("helper status \"%s\"", status.c_str());
        g_helper_status = status;
    }
    static bool exit_logged = false;
    DWORD code;
    if (!exit_logged && g_helper.ExitCode(code)) {
        Log("helper exited with code %s; gaze falls back to OSC, then head",
            rezgaze::HelperExitText(code).c_str());
        exit_logged = true;
    }
}

void RefreshEverySecond() {
    static LONGLONG last = 0;
    LONGLONG now = Qpc();
    if (now - last < g_qpf) return;
    last = now;
    LogHelperState();
    Config c = ReadConfig();
    {
        std::lock_guard<std::mutex> lock(g_cfg_mutex);
        if (c.mode != g_cfg.mode) Log("mode %s -> %s", ModeName(g_cfg.mode), ModeName(c.mode));
        g_cfg = c;
    }
    if (g_system && g_orig_role_to_index) {
        static bool system_logged = false;
        if (!system_logged) {
            std::string ts = DeviceString(vr::k_unTrackedDeviceIndex_Hmd, vr::Prop_TrackingSystemName_String);
            g_is_oculus = ts == "oculus";
            Log("HMD tracking system \"%s\", model \"%s\"%s", ts.c_str(),
                DeviceString(vr::k_unTrackedDeviceIndex_Hmd, vr::Prop_ModelNumber_String).c_str(),
                g_is_oculus ? ": the game tilts aim -39.4 deg, tilt=auto undoes it" : "");
            system_logged = true;
        }
        auto r = g_orig_role_to_index(g_system, vr::TrackedControllerRole_RightHand);
        auto l = g_orig_role_to_index(g_system, vr::TrackedControllerRole_LeftHand);
        if (r != g_right || l != g_left) {
            Log("hands: right=%d (class %d, \"%s\") left=%d (class %d, \"%s\")", (int)r, DeviceClass(r),
                r == vr::k_unTrackedDeviceIndexInvalid ? "" : DeviceString(r, vr::Prop_TrackingSystemName_String).c_str(),
                (int)l, DeviceClass(l),
                l == vr::k_unTrackedDeviceIndexInvalid ? "" : DeviceString(l, vr::Prop_TrackingSystemName_String).c_str());
        }
        g_right = r;
        g_left = l;
    }
}

Mat3 Rotation(const vr::HmdMatrix34_t& m) {
    Mat3 r;
    for (int c = 0; c < 3; ++c) r.col[c] = {m.m[0][c], m.m[1][c], m.m[2][c]};
    return r;
}

void SetRotation(vr::HmdMatrix34_t& m, const Mat3& r) {
    for (int c = 0; c < 3; ++c) {
        m.m[0][c] = (float)r.col[c].x;
        m.m[1][c] = (float)r.col[c].y;
        m.m[2][c] = (float)r.col[c].z;
    }
}

// Gaze direction in head space from the chosen source, smoothed there (so head turns aren't
// smoothed); straight ahead when neither source has fresh gaze.
V3 HeadGaze(const Config& c) {
    V3 helper_dir, osc_dir, g{0, 0, -1};
    double helper_age = -1, osc_age = -1;
    bool helper_tracked = false;
    std::string status;
    if (!g_helper.Read(helper_dir, helper_age, helper_tracked, status)) helper_age = -1;
    if (!g_osc.Read(osc_dir, osc_age)) osc_age = -1;
    rezgaze::Source src = rezgaze::ChooseSource(c.source, helper_age, helper_tracked, osc_age);
    if (src != g_source) {
        Log("gaze source %s -> %s (helper age %.0f ms%s, osc age %.0f ms)", rezgaze::SourceName(g_source),
            rezgaze::SourceName(src), helper_age, helper_tracked ? " tracked" : "", osc_age);
        g_source = src;
    }
    if (src == rezgaze::Source::Helper) g = helper_dir;
    else if (src == rezgaze::Source::Osc) g = osc_dir;
    if (src != rezgaze::Source::Head) g = rezgaze::ApplyCalibration(c.calib, g);
    LONGLONG now = Qpc();
    if (c.filter_euro && src != rezgaze::Source::Head) {
        // Filter each sample once, at its own time: this runs every frame, faster than samples
        // arrive, and a repeated sample with a tiny time step would read as a huge speed.
        LONGLONG sample = src == rezgaze::Source::Helper ? g_helper.SampleQpc() : g_osc.SampleQpc();
        g_euro.SetParams(c.euro_min_cutoff, c.euro_beta);
        return g_euro.Filter(g, static_cast<double>(sample) / g_qpf);
    }
    if (c.smooth_ms > 0 && g_smoothed_qpc) {
        double dt_ms = (now - g_smoothed_qpc) * 1000.0 / g_qpf;
        double k = 1 - std::exp(-dt_ms / c.smooth_ms);
        g = Norm(g_smoothed + (g - g_smoothed) * k);
    }
    g_smoothed = g;
    g_smoothed_qpc = now;
    return g;
}

std::atomic<long> g_rewrites{0};
// Largest |angle| (degrees) this 5 s: what the DLL aimed at, and what the game computed from it
// (sampled each frame). If the game's stays below ours, the game clamps; if both are small, the
// gaze itself is compressed.
double g_max_want_yaw = 0, g_max_want_pitch = 0, g_max_game_yaw = 0, g_max_game_pitch = 0;

// ---- controller buzzes (the mod can't draw in the headset, so it answers by haptics) --------

struct BuzzWindow {
    double from_ms, to_ms;
};
const std::vector<BuzzWindow> kBuzzStart{{0, 150}};
const std::vector<BuzzWindow> kBuzzDone{{0, 120}, {250, 370}};
const std::vector<BuzzWindow> kBuzzCancel{{0, 600}};

std::mutex g_buzz_mutex;
std::vector<BuzzWindow> g_buzz;
LONGLONG g_buzz_start = 0;

void StartBuzz(const std::vector<BuzzWindow>& pattern) {
    std::lock_guard<std::mutex> lock(g_buzz_mutex);
    g_buzz = pattern;
    g_buzz_start = Qpc();
}

// Every frame: pulse both hands while inside a window (TriggerHapticPulse, IVRSystem_015 slot 34,
// is a one-shot of up to ~4 ms, so it's repeated per frame).
void BuzzTick() {
    using FnPulse = void(__fastcall*)(void*, vr::TrackedDeviceIndex_t, uint32_t, unsigned short);
    bool on = false;
    {
        std::lock_guard<std::mutex> lock(g_buzz_mutex);
        if (g_buzz.empty()) return;
        double ms = (Qpc() - g_buzz_start) * 1000.0 / g_qpf;
        for (const BuzzWindow& w : g_buzz) on = on || (ms >= w.from_ms && ms < w.to_ms);
        if (ms >= g_buzz.back().to_ms) g_buzz.clear();
    }
    if (!on || !g_system) return;
    auto pulse = reinterpret_cast<FnPulse>((*reinterpret_cast<void***>(g_system))[34]);
    for (vr::TrackedDeviceIndex_t i : {g_right, g_left})
        if (i != vr::k_unTrackedDeviceIndexInvalid) pulse(g_system, i, 0, 3500);
}

// ---- calibration (mode=calibrate) ----------------------------------------------------------
// The cursor is pinned at a fixed world direction (where the head pointed when calibration
// began). The user holds their eyes on it and moves the head slowly, so each new gaze sample is
// a pair: what the tracker reports, and where the eyes must point (the pinned direction in head
// space). After calib_seconds the pairs are fitted (calib.cpp) and written to rez_gaze.ini as
// calib=..., with mode=gaze and the hand trims (yaw, pitch, gain_*) reset, since the fit replaces
// them.

struct CalibrationRun {
    bool active = false, done = false;
    V3 target_world{0, 0, -1};
    LONGLONG start = 0, deadline = 0, last_sample = 0, last_report = 0, last_head_qpc = 0;
    V3 last_head_fwd{0, 0, -1};
    rezgaze::CalibrationFit fit;
};
CalibrationRun g_calib;

// The raw gaze (no calibration, no filter) from the source ChooseSource picks, and its time.
bool RawGaze(const Config& c, V3& g, LONGLONG& sample) {
    V3 hd, od;
    double ha = -1, oa = -1;
    bool tracked = false;
    std::string status;
    if (!g_helper.Read(hd, ha, tracked, status)) ha = -1;
    if (!g_osc.Read(od, oa)) oa = -1;
    switch (rezgaze::ChooseSource(c.source, ha, tracked, oa)) {
        case rezgaze::Source::Helper: g = hd, sample = g_helper.SampleQpc(); return true;
        case rezgaze::Source::Osc: g = od, sample = g_osc.SampleQpc(); return true;
        default: return false;
    }
}

void WriteIni(const wchar_t* key, const std::string& value) {
    std::wstring v(value.begin(), value.end());
    WritePrivateProfileStringW(L"rez_gaze", key, v.c_str(), (g_dir + L"rez_gaze.ini").c_str());
}

// This frame's controller rotation during calibration: the cursor pinned in the world.
Mat3 Calibrate(const Config& c, const Mat3& head, double tilt) {
    constexpr double kToDeg = 57.29577951308232;
    LONGLONG now = Qpc();
    V3 fwd = Norm(head.col[2] * -1);
    if (!g_calib.active) {
        g_calib = CalibrationRun{};
        g_calib.active = true;
        g_calib.target_world = fwd;
        g_calib.start = g_calib.last_report = now;
        g_calib.deadline = now + static_cast<LONGLONG>(c.calib_seconds * g_qpf);
        Log("calibration: keep your eyes on the cursor and slowly move your head around it (circles, then "
            "up/down and left/right) for %.0f s",
            c.calib_seconds);
    }
    V3 t{Dot(head.col[0], g_calib.target_world), Dot(head.col[1], g_calib.target_world),
         Dot(head.col[2], g_calib.target_world)};
    double ty = std::atan2(t.x, -t.z) * kToDeg, tp = std::atan2(t.y, -t.z) * kToDeg;

    // Skip samples while the head turns fast: the cursor is drawn at the 60 Hz tick and lags it.
    double speed = 0;
    if (g_calib.last_head_qpc && now > g_calib.last_head_qpc) {
        double dot = std::max(-1.0, std::min(1.0, Dot(fwd, g_calib.last_head_fwd)));
        speed = std::acos(dot) * kToDeg / ((now - g_calib.last_head_qpc) / static_cast<double>(g_qpf));
    }
    g_calib.last_head_fwd = fwd;
    g_calib.last_head_qpc = now;

    V3 g;
    LONGLONG sample = 0;
    if (!g_calib.done && now - g_calib.start > g_qpf && t.z < 0 && std::hypot(ty, tp) < 35 && speed < 40 &&
        RawGaze(c, g, sample) && sample != g_calib.last_sample && g.z < 0) {
        g_calib.last_sample = sample;
        g_calib.fit.Add(std::atan2(g.x, -g.z) * kToDeg, std::atan2(g.y, -g.z) * kToDeg, ty, tp);
    }
    if (!g_calib.done && now - g_calib.last_report >= g_qpf) {
        double yr, pr;
        g_calib.fit.Coverage(yr, pr);
        Log("calibration: %zu samples, eyes covered %.0f deg across, %.0f deg up/down, %.0f s left",
            g_calib.fit.Count(), yr, pr, std::max(0.0, (g_calib.deadline - now) / static_cast<double>(g_qpf)));
        g_calib.last_report = now;
    }
    if (!g_calib.done && now >= g_calib.deadline) {
        rezgaze::Affine2 a;
        double rms;
        std::string why;
        if (g_calib.fit.Solve(a, rms, why)) {
            Log("calibration done: %zu samples, %.2f deg rms left; true yaw = %.3f*yaw %+.3f*pitch %+.2f, "
                "true pitch = %+.3f*yaw %.3f*pitch %+.2f",
                g_calib.fit.Count(), rms, a.m[0][0], a.m[0][1], a.m[0][2], a.m[1][0], a.m[1][1], a.m[1][2]);
            Log("calibration replaces the hand trims: yaw %.1f, pitch %.1f, gain %.2f/%.2f all reset", c.yaw, c.pitch,
                c.gain_yaw, c.gain_pitch);
            WriteIni(L"calib", rezgaze::FormatCalibration(a));
            WriteIni(L"yaw", "0");
            WriteIni(L"pitch", "0");
            WriteIni(L"gain_yaw", "1");
            WriteIni(L"gain_pitch", "1");
            WriteIni(L"mode", "gaze");
            g_calib.done = true;  // hold the cursor until the ini reload switches to gaze
            StartBuzz(kBuzzDone);
        } else {
            Log("calibration: not yet: %s; 5 s more", why.c_str());
            g_calib.deadline = now + 5 * g_qpf;
        }
    }
    return rezgaze::AimRotation(head, t, tilt, 0, 0);
}

void Rewrite(vr::TrackedDevicePose_t* poses, uint32_t n) {
    if (!poses || n == 0) return;
    Config c;
    {
        std::lock_guard<std::mutex> lock(g_cfg_mutex);
        c = g_cfg;
    }
    if (c.mode != Mode::Calibrate) g_calib.active = g_calib.done = false;
    if (c.mode == Mode::Log || !poses[0].bPoseIsValid) return;
    double tilt = c.tilt_auto ? (g_is_oculus ? 39.4 : 0.0) : c.tilt;
    Mat3 head = Rotation(poses[0].mDeviceToAbsoluteTracking);
    Mat3 aim;
    if (c.mode == Mode::Calibrate) {
        aim = Calibrate(c, head, tilt);
    } else {
        // The game projects the controller ray 400 m out, so the hand's offset from the head
        // doesn't matter: only the rotation is rewritten.
        V3 gaze = c.mode == Mode::Gaze ? rezgaze::ScaleGaze(HeadGaze(c), c.gain_yaw, c.gain_pitch) : V3{0, 0, -1};
        double want_yaw = std::atan2(gaze.x, -gaze.z) * 57.29577951308232 + c.yaw;
        double want_pitch = std::atan2(gaze.y, -gaze.z) * 57.29577951308232 + c.pitch;
        if (std::fabs(want_yaw) > g_max_want_yaw) g_max_want_yaw = std::fabs(want_yaw);
        if (std::fabs(want_pitch) > g_max_want_pitch) g_max_want_pitch = std::fabs(want_pitch);
        aim = rezgaze::AimRotation(head, gaze, tilt, c.yaw, c.pitch);
    }
    for (vr::TrackedDeviceIndex_t i : {c.right ? g_right : vr::k_unTrackedDeviceIndexInvalid,
                                       c.left ? g_left : vr::k_unTrackedDeviceIndexInvalid}) {
        if (i == vr::k_unTrackedDeviceIndexInvalid || i >= n || !poses[i].bPoseIsValid) continue;
        SetRotation(poses[i].mDeviceToAbsoluteTracking, aim);
        ++g_rewrites;
    }
}

// ---- hooks ---------------------------------------------------------------------------------

struct Counter {
    const char* name;
    std::atomic<long> n{0};
};
Counter g_count_wait{"WaitGetPoses"}, g_count_last{"GetLastPoses"}, g_count_last_one{"GetLastPoseForTrackedDeviceIndex"},
    g_count_abs{"GetDeviceToAbsoluteTrackingPose"}, g_count_state{"GetControllerState"},
    g_count_state_pose{"GetControllerStateWithPose"}, g_count_role{"GetTrackedDeviceIndexForControllerRole"},
    g_count_event_pose{"PollNextEventWithPose"};
Counter* g_counters[] = {&g_count_wait, &g_count_last, &g_count_last_one, &g_count_abs,
                         &g_count_state, &g_count_state_pose, &g_count_role, &g_count_event_pose};

void ReportEveryFiveSeconds() {
    static LONGLONG last = 0;
    LONGLONG now = Qpc();
    if (last == 0) last = now;
    if (now - last < 5 * g_qpf) return;
    last = now;
    std::string s;
    for (Counter* c : g_counters) {
        long v = c->n.exchange(0);
        if (v) s += std::string(" ") + c->name + "=" + std::to_string(v);
    }
    V3 hd, od;
    double helper_age = -1, osc_age = -1;
    bool tracked = false;
    std::string status;
    if (!g_helper.Read(hd, helper_age, tracked, status)) helper_age = -1;
    if (!g_osc.Read(od, osc_age)) osc_age = -1;
    Log("5 s:%s | rewrites=%ld | source %s | helper \"%s\" age %.0f ms%s, gaze %.3f %.3f %.3f | osc packets=%ld "
        "age %.0f ms",
        s.c_str(), g_rewrites.exchange(0), rezgaze::SourceName(g_source), status.c_str(), helper_age,
        tracked ? " tracked" : "", hd.x, hd.y, hd.z, g_osc.TakePacketCount(), osc_age);
    Log("     aim max |yaw| %.1f, |pitch| %.1f deg (DLL) vs %.1f, %.1f (game)", g_max_want_yaw, g_max_want_pitch,
        g_max_game_yaw, g_max_game_pitch);
    g_max_want_yaw = g_max_want_pitch = g_max_game_yaw = g_max_game_pitch = 0;
    RezState st;
    if (ReadRezState(st)) {
        static const char* kTypes[] = {"head+controller (drifts past 30 deg; use controller only)",
                                       "controller only", "head only (gaze can't aim)"};
        Log("     game: control type %d %s | input device %d%s | selected %s | isOculus %d | drift %.1f deg",
            st.control_type, st.control_type >= 0 && st.control_type <= 2 ? kTypes[st.control_type] : "?",
            st.input_device, st.input_device == 3 ? " (VR controller)" : " (not VR: press a controller button)",
            st.selected == 0 ? "left" : st.selected == 1 ? "right" : "?", st.is_oculus,
            st.drift * 57.29577951308232);
    }
}

void Tick() {
    BuzzTick();
    RezState st;
    if (ReadRezState(st)) {
        g_max_game_yaw = std::max(g_max_game_yaw, std::fabs(st.cursor_yaw * 57.29577951308232));
        g_max_game_pitch = std::max(g_max_game_pitch, std::fabs(st.cursor_pitch * 57.29577951308232));
    }
    // Start the helper a while after VR is running, not during Rez's VR start-up: started then,
    // its overlay app left Rez black and silent (2026-10-22). Once per process.
    static LONGLONG first = 0;
    LONGLONG now = Qpc();
    if (!first) first = now;
    Config c;
    {
        std::lock_guard<std::mutex> lock(g_cfg_mutex);
        c = g_cfg;
    }
    if (c.helper && now - first >= static_cast<LONGLONG>(c.helper_delay_s * g_qpf))
        g_helper.Start(g_dir + L"rez_gaze\\rez_gaze_helper.exe", GetCurrentProcessId(), Log);
    RefreshEverySecond();
    ReportEveryFiveSeconds();
}

using FnPoses = vr::EVRCompositorError(__fastcall*)(void*, vr::TrackedDevicePose_t*, uint32_t, vr::TrackedDevicePose_t*, uint32_t);
using FnLastOne = vr::EVRCompositorError(__fastcall*)(void*, vr::TrackedDeviceIndex_t, vr::TrackedDevicePose_t*, vr::TrackedDevicePose_t*);
using FnAbs = void(__fastcall*)(void*, vr::ETrackingUniverseOrigin, float, vr::TrackedDevicePose_t*, uint32_t);
using FnState = bool(__fastcall*)(void*, vr::TrackedDeviceIndex_t, void*, uint32_t);
using FnStatePose = bool(__fastcall*)(void*, vr::ETrackingUniverseOrigin, vr::TrackedDeviceIndex_t, void*, uint32_t, vr::TrackedDevicePose_t*);
using FnEventPose = bool(__fastcall*)(void*, vr::ETrackingUniverseOrigin, void*, uint32_t, vr::TrackedDevicePose_t*);

FnPoses g_orig_wait = nullptr, g_orig_last = nullptr;
FnLastOne g_orig_last_one = nullptr;
FnAbs g_orig_abs = nullptr;
FnState g_orig_state = nullptr;
FnStatePose g_orig_state_pose = nullptr;
FnEventPose g_orig_event_pose = nullptr;

vr::EVRCompositorError __fastcall HookWait(void* self, vr::TrackedDevicePose_t* r, uint32_t rn, vr::TrackedDevicePose_t* g, uint32_t gn) {
    auto e = g_orig_wait(self, r, rn, g, gn);
    ++g_count_wait.n;
    Tick();
    Rewrite(r, rn);
    Rewrite(g, gn);
    return e;
}

vr::EVRCompositorError __fastcall HookLast(void* self, vr::TrackedDevicePose_t* r, uint32_t rn, vr::TrackedDevicePose_t* g, uint32_t gn) {
    auto e = g_orig_last(self, r, rn, g, gn);
    ++g_count_last.n;
    Rewrite(r, rn);
    Rewrite(g, gn);
    return e;
}

vr::EVRCompositorError __fastcall HookLastOne(void* self, vr::TrackedDeviceIndex_t i, vr::TrackedDevicePose_t* p, vr::TrackedDevicePose_t* gp) {
    ++g_count_last_one.n;
    return g_orig_last_one(self, i, p, gp);  // logged only: one pose without the head to aim from
}

void __fastcall HookAbs(void* self, vr::ETrackingUniverseOrigin o, float t, vr::TrackedDevicePose_t* p, uint32_t n) {
    g_orig_abs(self, o, t, p, n);
    ++g_count_abs.n;
    Rewrite(p, n);
}

// ---- the calibration combination (both thumbsticks held) -------------------------------------

constexpr uint64_t kStickButton = 1ull << vr::k_EButton_SteamVR_Touchpad;  // Touch thumbstick click
rezgaze::ComboDetector g_combo(rezgaze::ComboDetector::Both, 2);
std::mutex g_combo_mutex;
bool g_stick_left = false, g_stick_right = false;
Mode g_mode_before_calib = Mode::Gaze;

// The combination fired: start calibration, or cancel a running one.
void OnCombo() {
    std::lock_guard<std::mutex> lock(g_cfg_mutex);
    if (g_cfg.mode != Mode::Calibrate) {
        g_mode_before_calib = g_cfg.mode;
        g_cfg.mode = Mode::Calibrate;
        WriteIni(L"mode", "calibrate");
        Log("thumbsticks held: calibration starts (hold them again to cancel)");
        StartBuzz(kBuzzStart);
    } else {
        g_cfg.mode = g_mode_before_calib;
        WriteIni(L"mode", ModeName(g_mode_before_calib));
        Log("thumbsticks held: calibration cancelled, back to %s", ModeName(g_mode_before_calib));
        StartBuzz(kBuzzCancel);
    }
}

bool __fastcall HookState(void* self, vr::TrackedDeviceIndex_t i, void* s, uint32_t n) {
    ++g_count_state.n;
    bool ok = g_orig_state(self, i, s, n);
    if (!ok || !s || n < sizeof(vr::VRControllerState_t) || (i != g_left && i != g_right)) return ok;
    auto* state = static_cast<vr::VRControllerState_t*>(s);
    Config c;
    {
        std::lock_guard<std::mutex> lock(g_cfg_mutex);
        c = g_cfg;
    }
    if (c.calib_hold_s <= 0) return ok;
    bool fired = false, mask = false;
    {
        std::lock_guard<std::mutex> lock(g_combo_mutex);
        bool down = (state->ulButtonPressed & kStickButton) != 0;
        bool& side = i == g_left ? g_stick_left : g_stick_right;
        if (down != side) Log("%s thumbstick %s", i == g_left ? "left" : "right", down ? "down" : "up");
        side = down;
        g_combo.Set(c.calib_combo_one ? rezgaze::ComboDetector::One : rezgaze::ComboDetector::Both, c.calib_hold_s);
        fired = g_combo.Update(g_stick_left, g_stick_right, Qpc() / static_cast<double>(g_qpf));
        mask = g_combo.Masking();
    }
    if (fired) OnCombo();
    if (mask) state->ulButtonPressed &= ~kStickButton;  // the hold that triggered it isn't Rez's
    return ok;
}

bool __fastcall HookStatePose(void* self, vr::ETrackingUniverseOrigin o, vr::TrackedDeviceIndex_t i, void* s, uint32_t n, vr::TrackedDevicePose_t* p) {
    ++g_count_state_pose.n;
    return g_orig_state_pose(self, o, i, s, n, p);  // logged only for now
}

vr::TrackedDeviceIndex_t __fastcall HookRole(void* self, vr::ETrackedControllerRole r) {
    ++g_count_role.n;
    return g_orig_role_to_index(self, r);
}

bool __fastcall HookEventPose(void* self, vr::ETrackingUniverseOrigin o, void* e, uint32_t n, vr::TrackedDevicePose_t* p) {
    ++g_count_event_pose.n;
    return g_orig_event_pose(self, o, e, n, p);
}

template <typename Fn>
void Patch(void* obj, int slot, Fn hook, Fn& orig, const char* name) {
    void** vtable = *reinterpret_cast<void***>(obj);
    if (vtable[slot] == reinterpret_cast<void*>(hook)) return;
    DWORD old;
    VirtualProtect(&vtable[slot], sizeof(void*), PAGE_READWRITE, &old);
    orig = reinterpret_cast<Fn>(vtable[slot]);
    vtable[slot] = reinterpret_cast<void*>(hook);
    VirtualProtect(&vtable[slot], sizeof(void*), old, &old);
    Log("hooked %s (slot %d)", name, slot);
}

// ---- the real DLL --------------------------------------------------------------------------

using FnGetGeneric = void*(__cdecl*)(const char*, vr::EVRInitError*);
FnGetGeneric g_real_get_generic = nullptr;

void Init(HMODULE self) {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(self, path, MAX_PATH);
    g_dir = path;
    g_dir.resize(g_dir.find_last_of(L'\\') + 1);
    g_log = _wfopen((g_dir + L"rez_gaze.log").c_str(), L"w");
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    g_qpf = f.QuadPart;
    g_cfg = ReadConfig();
    Log("rez_gaze %s loaded; mode %s, hands %s%s, tilt %s, pitch %.1f, yaw %.1f, smooth %.0f ms", REZ_GAZE_VERSION,
        ModeName(g_cfg.mode), g_cfg.right ? "R" : "", g_cfg.left ? "L" : "",
        g_cfg.tilt_auto ? "auto" : std::to_string(g_cfg.tilt).c_str(), g_cfg.pitch, g_cfg.yaw, g_cfg.smooth_ms);
    FindRez();
    HMODULE real = LoadLibraryW((g_dir + L"openvr_api_real.dll").c_str());
    if (!real) {
        Log("can't load openvr_api_real.dll (error %lu)", GetLastError());
        return;
    }
    g_real_get_generic = reinterpret_cast<FnGetGeneric>(GetProcAddress(real, "VR_GetGenericInterface"));
    g_osc.Start(g_cfg.port, Log);
}

}  // namespace

extern "C" void* __cdecl VR_GetGenericInterface(const char* version, vr::EVRInitError* err) {
    if (!g_real_get_generic) {
        if (err) *err = vr::VRInitError_Init_InterfaceNotFound;
        return nullptr;
    }
    void* obj = g_real_get_generic(version, err);
    static std::mutex m;
    std::lock_guard<std::mutex> lock(m);
    static std::string seen;
    if (seen.find(std::string(version) + ";") == std::string::npos) {
        seen += std::string(version) + ";";
        Log("VR_GetGenericInterface(%s) -> %p", version, obj);
    }
    if (!obj) return obj;
    if (std::strcmp(version, "IVRSystem_015") == 0) {
        g_system = static_cast<vr::IVRSystem*>(obj);
        // The helper is started from Tick (WaitGetPoses), helper_delay_s after VR is running;
        // flat runs never get there.
        Patch(obj, 10, &HookAbs, g_orig_abs, "IVRSystem::GetDeviceToAbsoluteTrackingPose");
        Patch(obj, 17, &HookRole, g_orig_role_to_index, "IVRSystem::GetTrackedDeviceIndexForControllerRole");
        Patch(obj, 29, &HookEventPose, g_orig_event_pose, "IVRSystem::PollNextEventWithPose");
        Patch(obj, 32, &HookState, g_orig_state, "IVRSystem::GetControllerState");
        Patch(obj, 33, &HookStatePose, g_orig_state_pose, "IVRSystem::GetControllerStateWithPose");
    } else if (std::strcmp(version, "IVRCompositor_015") == 0) {
        Patch(obj, 2, &HookWait, g_orig_wait, "IVRCompositor::WaitGetPoses");
        Patch(obj, 3, &HookLast, g_orig_last, "IVRCompositor::GetLastPoses");
        Patch(obj, 4, &HookLastOne, g_orig_last_one, "IVRCompositor::GetLastPoseForTrackedDeviceIndex");
    }
    return obj;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        Init(inst);
    }
    return TRUE;
}
