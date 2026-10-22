#pragma once

#include "common.h"

#include <openvr.h>

struct OpenVrOptions {
    std::string app = "overlay";  // overlay | background | scene
    std::string bind = "/user/head/eyetracking";
    bool legacy_check = false;    // watch GetControllerState after the manifest is set
};

class OpenVrSession {
public:
    bool Init(const OpenVrOptions& opt, Recorder& rec);
    void Poll(Recorder& rec, bool record);
    void Shutdown();

private:
    vr::IVRSystem* sys_ = nullptr;
    vr::VRActionSetHandle_t set_ = vr::k_ulInvalidActionSetHandle;
    vr::VRActionHandle_t action_ = vr::k_ulInvalidActionHandle;
    vr::TrackedDeviceIndex_t legacy_hand_ = vr::k_unTrackedDeviceIndexInvalid;
    uint64_t legacy_buttons_ = 0;
    double legacy_report_ms_ = -1e9;
};

// OSC route: listens for vrlink's OSC eye data on 127.0.0.1:port.
int RunOscProbe(int port, double seconds, Recorder& rec);
// replay mode: sends gaze over OSC as vrlink does, from a probe CSV or as a circle.
int RunOscReplay(const std::string& csv, double circle_deg, int port, double seconds, int hz, Recorder& rec);

// OpenXR route: XR_EXT_eye_gaze_interaction in an XR_MND_headless session (no graphics, no
// frame loop), so it can run beside a game; or, with frames, a D3D11 session that submits
// a cleared frame each refresh (SteamVR turned the headless session into a scene app anyway,
// and gave it no gaze).
class OpenXrSession {
public:
    bool Init(Recorder& rec, bool frames = false);
    void Poll(Recorder& rec);
    void Shutdown();
    ~OpenXrSession() { Shutdown(); }

private:
    struct Impl;
    Impl* impl_ = nullptr;
};
