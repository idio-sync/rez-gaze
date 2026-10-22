// rez_gaze_helper: reads the Steam Frame's gaze through OpenVR's "eyetracking" action and
// publishes it, head-relative, to Local\rez_gaze_<parent pid> for the stand-in openvr_api.dll.
// The DLL starts it once the game has initialised VR; it exits with the game, or when SteamVR
// quits. It lives in REZ\rez_gaze\ with its own OpenVR 2.15.6 openvr_api.dll: in REZ\ itself it
// would load the stand-in, which forwards to Rez's 2021 DLL.
//
//   rez_gaze_helper.exe --parent <pid> [--app overlay|background] [--hz 250] [--fake]
// --fake skips OpenVR and publishes a tracked gaze sweeping a 10 degree circle every 4 s, for
// testing the DLL and the helper's lifetime without SteamVR.
// Exit codes: 0 normal, 2 bad arguments, 3 VR init or manifest failed, 4 shared memory failed.
#include "../shared/gaze_shm.h"

#include <openvr.h>
#include <timeapi.h>

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

FILE* g_log = nullptr;

void Log(const char* fmt, ...) {
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

std::filesystem::path ExeDir() {
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return std::filesystem::path(std::wstring(buf, n)).parent_path();
}

void SetStatus(rezgaze::GazeShmWriter& w, const std::string& s) {
    w.SetStatus(s.c_str());
    Log("status %s", s.c_str());
}

// The action manifest and bindings (one per HMD controller type the Frame may report), as the
// probe writes them; the binding path was confirmed on the Frame (docs/findings.md, run 3).
std::filesystem::path WriteManifest() {
    std::filesystem::path dir = ExeDir() / "openvr_input";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const char* types[] = {"frame_hmd", "remote_hmd"};
    std::ofstream m(dir / "actions.json");
    m << "{\n  \"default_bindings\": [\n";
    for (int i = 0; i < 2; ++i) {
        m << "    {\"controller_type\": \"" << types[i] << "\", \"binding_url\": \"bindings_" << types[i]
          << ".json\"}" << (i == 0 ? ",\n" : "\n");
    }
    m << "  ],\n"
         "  \"actions\": [ {\"name\": \"/actions/gaze/in/gaze\", \"type\": \"eyetracking\"} ],\n"
         "  \"action_sets\": [ {\"name\": \"/actions/gaze\", \"usage\": \"single\"} ],\n"
         "  \"localization\": [ {\"language_tag\": \"en_US\", \"/actions/gaze\": \"Gaze\",\n"
         "                      \"/actions/gaze/in/gaze\": \"Eye gaze\"} ]\n"
         "}\n";
    for (const char* type : types) {
        std::ofstream b(dir / (std::string("bindings_") + type + ".json"));
        b << "{\n  \"action_manifest_version\": 0,\n  \"controller_type\": \"" << type
          << "\",\n  \"name\": \"rez-gaze helper\",\n  \"description\": \"rez-gaze helper\",\n"
             "  \"bindings\": {\n    \"/actions/gaze\": {\n      \"eyetracking\": [\n"
             "        {\"output\": \"/actions/gaze/in/gaze\", \"path\": \"/user/head/eyetracking\"}\n"
             "      ]\n    }\n  }\n}\n";
    }
    return dir / "actions.json";
}

}  // namespace

int main(int argc, char** argv) {
    g_log = _wfopen((ExeDir() / "rez_gaze_helper.log").c_str(), L"w");
    DWORD parent_pid = 0;
    std::string app = "overlay";
    int hz = 250;
    bool fake = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        std::string v = i + 1 < argc ? argv[i + 1] : "";
        if (a == "--fake") fake = true;
        else if (a == "--parent") parent_pid = std::strtoul(v.c_str(), nullptr, 10), ++i;
        else if (a == "--app") app = v, ++i;
        else if (a == "--hz") hz = std::atoi(v.c_str()), ++i;
        else {
            Log("unknown argument %s", a.c_str());
            return 2;
        }
    }
    if (parent_pid == 0 || (app != "overlay" && app != "background") || hz < 1) {
        Log("usage: rez_gaze_helper --parent <pid> [--app overlay|background] [--hz N]");
        return 2;
    }
    Log("rez_gaze_helper for pid %lu, app %s, %d Hz%s", parent_pid, app.c_str(), hz, fake ? ", fake gaze" : "");

    rezgaze::GazeShmWriter shm;
    if (!shm.Create(rezgaze::ShmName(parent_pid))) {
        Log("can't create shared memory (error %lu)", GetLastError());
        return 4;
    }
    SetStatus(shm, "starting");
    HANDLE parent = OpenProcess(SYNCHRONIZE, FALSE, parent_pid);
    if (!parent) {
        SetStatus(shm, "parent exited");
        return 0;
    }

    vr::IVRSystem* sys = nullptr;
    vr::VRActionSetHandle_t set = vr::k_ulInvalidActionSetHandle;
    vr::VRActionHandle_t action = vr::k_ulInvalidActionHandle;
    if (!fake) {
        vr::EVRInitError err = vr::VRInitError_None;
        sys = vr::VR_Init(&err, app == "background" ? vr::VRApplication_Background : vr::VRApplication_Overlay);
        if (err != vr::VRInitError_None) {
            SetStatus(shm, std::string("vr init failed: ") + vr::VR_GetVRInitErrorAsEnglishDescription(err));
            return 3;
        }
        vr::EVRInputError ie = vr::VRInput()->SetActionManifestPath(WriteManifest().string().c_str());
        if (ie == vr::VRInputError_None) vr::VRInput()->GetActionSetHandle("/actions/gaze", &set);
        if (ie == vr::VRInputError_None) ie = vr::VRInput()->GetActionHandle("/actions/gaze/in/gaze", &action);
        if (ie != vr::VRInputError_None) {
            SetStatus(shm, "manifest failed: input error " + std::to_string(ie));
            vr::VR_Shutdown();
            return 3;
        }
    }
    SetStatus(shm, "running");

    timeBeginPeriod(1);
    DWORD sleep_ms = static_cast<DWORD>(1000 / hz);
    uint64_t count = 0, tracked = 0, window = 0, window_tracked = 0;
    ULONGLONG last_report = GetTickCount64();
    std::string end = "parent exited";
    for (;;) {
        if (WaitForSingleObject(parent, 0) == WAIT_OBJECT_0) break;
        rezgaze::GazeSampleShm s{{0, 0, -1}, 0, 0, 0};
        if (fake) {
            double a = GetTickCount64() / 4000.0 * 2 * 3.14159265358979, r = 10 * 3.14159265358979 / 180;
            double x = std::tan(r * std::cos(a)), y = std::tan(r * std::sin(a)), l = std::sqrt(x * x + y * y + 1);
            s.dir[0] = static_cast<float>(x / l);
            s.dir[1] = static_cast<float>(y / l);
            s.dir[2] = static_cast<float>(-1 / l);
            s.flags = rezgaze::kGazeActive | rezgaze::kGazeValid | rezgaze::kGazeTracked;
        }
        vr::VREvent_t ev;
        bool quit = false;
        while (sys && sys->PollNextEvent(&ev, sizeof ev)) {
            if (ev.eventType == vr::VREvent_Quit) {
                sys->AcknowledgeQuit_Exiting();
                quit = true;
            }
        }
        if (quit) {
            end = "steamvr quit";
            break;
        }

        vr::VREyeTrackingData_t d{};
        vr::EVRInputError ge = vr::VRInputError_NoData;
        vr::TrackedDevicePose_t head{};
        if (sys) {
            vr::VRActiveActionSet_t active{};
            active.ulActionSet = set;
            vr::VRInput()->UpdateActionState(&active, sizeof active, 1);
            ge = vr::VRInput()->GetEyeTrackingDataRelativeToNow(action, vr::TrackingUniverseStanding, 0.0f, &d,
                                                                sizeof d);
            sys->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseStanding, 0.0f, &head, 1);
        }
        if (ge == vr::VRInputError_None) {
            if (d.bActive) s.flags |= rezgaze::kGazeActive;
            if (d.bValid && head.bPoseIsValid) {
                // Origin and target are in standing space; the transposed head rotation takes
                // the direction into head space.
                const auto& m = head.mDeviceToAbsoluteTracking.m;
                double x = d.vGazeTarget.v[0] - d.vGazeOrigin.v[0], y = d.vGazeTarget.v[1] - d.vGazeOrigin.v[1],
                       z = d.vGazeTarget.v[2] - d.vGazeOrigin.v[2];
                double hx = m[0][0] * x + m[1][0] * y + m[2][0] * z, hy = m[0][1] * x + m[1][1] * y + m[2][1] * z,
                       hz2 = m[0][2] * x + m[1][2] * y + m[2][2] * z;
                double l = std::sqrt(hx * hx + hy * hy + hz2 * hz2);
                if (l > 1e-9) {
                    s.dir[0] = static_cast<float>(hx / l);
                    s.dir[1] = static_cast<float>(hy / l);
                    s.dir[2] = static_cast<float>(hz2 / l);
                    s.flags |= rezgaze::kGazeValid;
                    if (d.bTracked) s.flags |= rezgaze::kGazeTracked;
                }
            }
        }
        LARGE_INTEGER q;
        QueryPerformanceCounter(&q);
        s.qpc = q.QuadPart;
        s.count = ++count;
        shm.Publish(s);
        ++window;
        if (s.flags & rezgaze::kGazeTracked) ++tracked, ++window_tracked;

        ULONGLONG now = GetTickCount64();
        if (now - last_report >= 5000) {
            Log("5 s: %llu samples, %.0f%% tracked", window, window ? 100.0 * window_tracked / window : 0.0);
            window = window_tracked = 0;
            last_report = now;
        }
        Sleep(sleep_ms);
    }
    timeEndPeriod(1);
    Log("%llu samples, %llu tracked", count, tracked);
    SetStatus(shm, end);
    if (sys) vr::VR_Shutdown();
    CloseHandle(parent);
    return 0;
}
