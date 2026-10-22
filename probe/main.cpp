// gaze_probe: can a program read the Steam Frame's eye gaze while another VR game (Rez
// Infinite) has focus? See README.md for the test runs.
#include "probes.h"

#include <windows.h>
#include <timeapi.h>

#include <cstdlib>
#include <cstring>

std::atomic<bool> g_stop{false};

namespace {

BOOL WINAPI OnCtrl(DWORD) {
    g_stop = true;
    return TRUE;
}

void Usage() {
    std::printf(
        "gaze_probe <mode> [options]\n"
        "\n"
        "modes:\n"
        "  openvr   IVRInput \"eyetracking\" action. Start Rez (or any VR game) first, then this.\n"
        "  openxr   XR_EXT_eye_gaze_interaction in an XR_MND_headless session, beside the game.\n"
        "  inproc   Stand-in for a mod inside the game: registers as the scene app (OpenVR),\n"
        "           then reads gaze both ways from this one process. Run with NO game open.\n"
        "\n"
        "options:\n"
        "  --app overlay|background|scene   OpenVR app type for openvr mode (default overlay)\n"
        "  --bind PATH       OpenVR binding path (default /user/head/eyetracking)\n"
        "  --seconds N       stop after N seconds (default: run until Ctrl+C)\n"
        "  --hz N            polls per second (default 250)\n"
        "  --legacy-check    in openvr/inproc: also watch the old GetControllerState API, to see\n"
        "                    whether setting an action manifest switches it off\n"
        "\n"
        "Logs go to logs\\gaze-<mode>-<time>.csv beside the exe.\n");
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        Usage();
        return 2;
    }
    std::string mode = argv[1];
    OpenVrOptions vro;
    double seconds = 0;
    int hz = 250;
    for (int i = 2; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--app") vro.app = next();
        else if (a == "--bind") vro.bind = next();
        else if (a == "--seconds") seconds = std::atof(next().c_str());
        else if (a == "--hz") hz = std::atoi(next().c_str());
        else if (a == "--legacy-check") vro.legacy_check = true;
        else {
            std::printf("unknown option %s\n\n", a.c_str());
            Usage();
            return 2;
        }
    }
    if (hz < 1) hz = 1;
    SetConsoleCtrlHandler(OnCtrl, TRUE);
    timeBeginPeriod(1);
    DWORD sleep_ms = static_cast<DWORD>(1000 / hz);

    int rc = 0;
    if (mode == "openvr") {
        Recorder rec("openvr-" + vro.app);
        OpenVrSession vr;
        if (!vr.Init(vro, rec)) return 1;
        while (!g_stop && (seconds <= 0 || rec.NowMs() < seconds * 1000)) {
            vr.Poll(rec, true);
            Sleep(sleep_ms);
        }
        vr.Shutdown();
        rec.Summary();
    } else if (mode == "openxr") {
        Recorder rec("openxr");
        OpenXrSession xr;
        if (!xr.Init(rec)) return 1;
        while (!g_stop && (seconds <= 0 || rec.NowMs() < seconds * 1000)) {
            xr.Poll(rec);
            Sleep(sleep_ms);
        }
        xr.Shutdown();
        rec.Summary();
    } else if (mode == "inproc") {
        vro.app = "scene";
        Recorder vr_rec("inproc-openvr");
        Recorder xr_rec("inproc-openxr");
        OpenVrSession vr;
        if (!vr.Init(vro, vr_rec)) return 1;
        OpenXrSession xr;
        bool have_xr = xr.Init(xr_rec);
        if (!have_xr) xr_rec.Note("OpenXR side unavailable; continuing with OpenVR only");
        while (!g_stop && (seconds <= 0 || vr_rec.NowMs() < seconds * 1000)) {
            vr.Poll(vr_rec, true);
            if (have_xr) xr.Poll(xr_rec);
            Sleep(sleep_ms);
        }
        xr.Shutdown();
        vr.Shutdown();
        vr_rec.Summary();
        if (have_xr) xr_rec.Summary();
    } else {
        Usage();
        rc = 2;
    }
    timeEndPeriod(1);
    return rc;
}
