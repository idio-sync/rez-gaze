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

// OpenXR route: XR_EXT_eye_gaze_interaction in an XR_MND_headless session (no graphics, no
// frame loop), so it can run beside a game.
class OpenXrSession {
public:
    bool Init(Recorder& rec);
    void Poll(Recorder& rec);
    void Shutdown();
    ~OpenXrSession() { Shutdown(); }

private:
    struct Impl;
    Impl* impl_ = nullptr;
};
