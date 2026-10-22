// OpenVR route: IVRInput's "eyetracking" action (SteamVR 2.15+ / OpenVR SDK 2.15.6).
// Valve documents the action type but not its binding path; the Frame's streaming driver
// (vrlink) creates its component as "/eyetracking" on the HMD, so the default is
// /user/head/eyetracking and --bind overrides it.
#include "probes.h"

#include <openvr.h>
#include <windows.h>

#include <filesystem>
#include <fstream>

namespace {

const char* InputErrorName(vr::EVRInputError e) {
    switch (e) {
        case vr::VRInputError_None: return "ok";
        case vr::VRInputError_NameNotFound: return "NameNotFound";
        case vr::VRInputError_WrongType: return "WrongType";
        case vr::VRInputError_InvalidHandle: return "InvalidHandle";
        case vr::VRInputError_InvalidParam: return "InvalidParam";
        case vr::VRInputError_NoSteam: return "NoSteam";
        case vr::VRInputError_MaxCapacityReached: return "MaxCapacityReached";
        case vr::VRInputError_IPCError: return "IPCError";
        case vr::VRInputError_NoActiveActionSet: return "NoActiveActionSet";
        case vr::VRInputError_InvalidDevice: return "InvalidDevice";
        case vr::VRInputError_InvalidSkeleton: return "InvalidSkeleton";
        case vr::VRInputError_InvalidBoneCount: return "InvalidBoneCount";
        case vr::VRInputError_InvalidCompressedData: return "InvalidCompressedData";
        case vr::VRInputError_NoData: return "NoData";
        case vr::VRInputError_BufferTooSmall: return "BufferTooSmall";
        case vr::VRInputError_MismatchedActionManifest: return "MismatchedActionManifest";
        case vr::VRInputError_MissingSkeletonData: return "MissingSkeletonData";
        case vr::VRInputError_InvalidBoneIndex: return "InvalidBoneIndex";
        case vr::VRInputError_InvalidPriority: return "InvalidPriority";
        case vr::VRInputError_PermissionDenied: return "PermissionDenied";
        case vr::VRInputError_InvalidRenderModel: return "InvalidRenderModel";
        default: return "InputError?";
    }
}

std::string DeviceString(vr::IVRSystem* sys, vr::TrackedDeviceIndex_t i, vr::ETrackedDeviceProperty p) {
    char buf[256] = {};
    sys->GetStringTrackedDeviceProperty(i, p, buf, sizeof buf);
    return buf;
}

// Writes the action manifest and one binding file per HMD controller type the Frame may
// report, into a folder beside the exe. SteamVR needs an absolute manifest path.
std::filesystem::path WriteManifest(const std::string& bind) {
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::filesystem::path dir = std::filesystem::path(std::wstring(buf, n)).parent_path() / "openvr_input";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);

    const char* types[] = {"remote_hmd", "frame_hmd"};
    std::ofstream m(dir / "actions.json");
    m << "{\n  \"default_bindings\": [\n";
    for (int i = 0; i < 2; ++i) {
        m << "    {\"controller_type\": \"" << types[i] << "\", \"binding_url\": \"bindings_"
          << types[i] << ".json\"}" << (i == 0 ? ",\n" : "\n");
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
          << "\",\n  \"name\": \"rez-gaze probe\",\n  \"description\": \"rez-gaze probe\",\n"
             "  \"bindings\": {\n    \"/actions/gaze\": {\n      \"eyetracking\": [\n"
             "        {\"output\": \"/actions/gaze/in/gaze\", \"path\": \""
          << bind << "\"}\n      ]\n    }\n  }\n}\n";
    }
    return dir / "actions.json";
}

void LogEvent(vr::IVRSystem* sys, const vr::VREvent_t& ev, Recorder& rec) {
    switch (ev.eventType) {
        case vr::VREvent_InputFocusCaptured:
        case vr::VREvent_InputFocusReleased:
        case vr::VREvent_SceneApplicationChanged:
        case vr::VREvent_SceneApplicationStateChanged:
        case vr::VREvent_ActionBindingReloaded:
        case vr::VREvent_Input_BindingLoadFailed:
        case vr::VREvent_Input_BindingLoadSuccessful:
        case vr::VREvent_Input_ActionManifestReloaded:
        case vr::VREvent_Input_ActionManifestLoadFailed:
        case vr::VREvent_Input_BindingsUpdated:
        case vr::VREvent_DashboardActivated:
        case vr::VREvent_DashboardDeactivated:
        case vr::VREvent_TrackedDeviceUserInteractionStarted:
        case vr::VREvent_TrackedDeviceUserInteractionEnded:
            rec.Note(std::string("event ") + sys->GetEventTypeNameFromEnum(
                                                 static_cast<vr::EVREventType>(ev.eventType)));
            break;
        default:
            break;
    }
}

}  // namespace

bool OpenVrSession::Init(const OpenVrOptions& opt, Recorder& rec) {
    vr::EVRApplicationType type = vr::VRApplication_Overlay;
    if (opt.app == "background") type = vr::VRApplication_Background;
    else if (opt.app == "scene") type = vr::VRApplication_Scene;
    else if (opt.app != "overlay") {
        rec.Note("unknown --app " + opt.app + " (overlay, background or scene)");
        return false;
    }

    vr::EVRInitError err = vr::VRInitError_None;
    sys_ = vr::VR_Init(&err, type);
    if (err != vr::VRInitError_None) {
        rec.Note(std::string("VR_Init(") + opt.app + ") failed: " +
                 vr::VR_GetVRInitErrorAsEnglishDescription(err));
        sys_ = nullptr;
        return false;
    }
    rec.Note("OpenVR " + opt.app + " app; runtime " + vr::VRSystem()->GetRuntimeVersion());
    vr::TrackedDeviceIndex_t hmd = vr::k_unTrackedDeviceIndex_Hmd;
    rec.Note("HMD: " + DeviceString(sys_, hmd, vr::Prop_ModelNumber_String) + " / driver " +
             DeviceString(sys_, hmd, vr::Prop_TrackingSystemName_String) + " / controller type " +
             DeviceString(sys_, hmd, vr::Prop_ControllerType_String));

    if (opt.legacy_check) {
        // Baseline before the manifest: does the old GetControllerState API see a controller?
        legacy_hand_ = sys_->GetTrackedDeviceIndexForControllerRole(vr::TrackedControllerRole_RightHand);
        rec.Note("legacy check: right hand is device " + std::to_string(legacy_hand_));
    }

    std::filesystem::path manifest = WriteManifest(opt.bind);
    rec.Note("binding path " + opt.bind + "; manifest " + manifest.string());
    vr::EVRInputError ie = vr::VRInput()->SetActionManifestPath(manifest.string().c_str());
    if (ie != vr::VRInputError_None) {
        rec.Note(std::string("SetActionManifestPath failed: ") + InputErrorName(ie));
        return false;
    }
    vr::VRInput()->GetActionSetHandle("/actions/gaze", &set_);
    ie = vr::VRInput()->GetActionHandle("/actions/gaze/in/gaze", &action_);
    if (ie != vr::VRInputError_None) {
        rec.Note(std::string("GetActionHandle failed: ") + InputErrorName(ie));
        return false;
    }
    return true;
}

void OpenVrSession::Poll(Recorder& rec, bool record) {
    vr::VREvent_t ev;
    while (sys_->PollNextEvent(&ev, sizeof ev)) {
        if (ev.eventType == vr::VREvent_Quit) {
            rec.Note("SteamVR asked us to quit");
            sys_->AcknowledgeQuit_Exiting();
            g_stop = true;
        }
        LogEvent(sys_, ev, rec);
    }

    vr::VRActiveActionSet_t active{};
    active.ulActionSet = set_;
    vr::EVRInputError ue = vr::VRInput()->UpdateActionState(&active, sizeof active, 1);

    vr::VREyeTrackingData_t d{};
    vr::EVRInputError ge = vr::VRInput()->GetEyeTrackingDataRelativeToNow(
        action_, vr::TrackingUniverseStanding, 0.0f, &d, sizeof d);

    vr::TrackedDevicePose_t head{};
    sys_->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseStanding, 0.0f, &head, 1);

    Sample s;
    s.result = ue != vr::VRInputError_None ? std::string("update:") + InputErrorName(ue)
                                           : InputErrorName(ge);
    s.active = d.bActive;
    s.tracked = ge == vr::VRInputError_None && d.bTracked;
    if (ge == vr::VRInputError_None && d.bValid && head.bPoseIsValid) {
        // Origin and target come in standing-universe coordinates; take them into head space
        // with the transposed head rotation.
        const auto& m = head.mDeviceToAbsoluteTracking.m;
        Vec3 o{d.vGazeOrigin.v[0], d.vGazeOrigin.v[1], d.vGazeOrigin.v[2]};
        Vec3 t{d.vGazeTarget.v[0], d.vGazeTarget.v[1], d.vGazeTarget.v[2]};
        Vec3 dir = Normalize(Sub(t, o));
        Vec3 rel = Sub(o, Vec3{m[0][3], m[1][3], m[2][3]});
        auto to_head = [&](Vec3 v) {
            return Vec3{m[0][0] * v.x + m[1][0] * v.y + m[2][0] * v.z,
                        m[0][1] * v.x + m[1][1] * v.y + m[2][1] * v.z,
                        m[0][2] * v.x + m[1][2] * v.y + m[2][2] * v.z};
        };
        s.valid = true;
        s.dir_head = to_head(dir);
        s.origin_head = to_head(rel);
    }
    if (record) rec.Add(s);

    if (legacy_hand_ != vr::k_unTrackedDeviceIndexInvalid) {
        vr::VRControllerState_t cs{};
        bool ok = sys_->GetControllerState(legacy_hand_, &cs, sizeof cs);
        double now = rec.NowMs();
        if (ok && cs.ulButtonPressed != legacy_buttons_) {
            rec.Note("legacy input still works: right-hand buttons 0x" +
                     std::to_string(cs.ulButtonPressed));
            legacy_buttons_ = cs.ulButtonPressed;
        }
        if (now - legacy_report_ms_ > 5000) {
            rec.Note(std::string("legacy GetControllerState: ") + (ok ? "ok" : "false") +
                     ", packet " + std::to_string(cs.unPacketNum) +
                     " (press buttons: a change in buttons is logged)");
            legacy_report_ms_ = now;
        }
    }
}

void OpenVrSession::Shutdown() {
    if (sys_) vr::VR_Shutdown();
    sys_ = nullptr;
}
