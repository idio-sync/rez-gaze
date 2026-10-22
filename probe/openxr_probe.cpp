// OpenXR route. The loader is loaded at run time (band3's approach): openxr_loader.dll from the
// usual search path, else the copy SteamVR ships. Everything else comes from
// xrGetInstanceProcAddr, so nothing links against OpenXR.
#include "probes.h"

#include <windows.h>
#include <unknwn.h>  // openxr_platform.h's Win32 section names IUnknown

#define XR_USE_PLATFORM_WIN32
#define XR_NO_PROTOTYPES
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <cstring>
#include <vector>

namespace {

#define XR_FUNCS(X)                                \
    X(xrGetInstanceProperties)                     \
    X(xrGetSystem)                                 \
    X(xrGetSystemProperties)                       \
    X(xrDestroyInstance)                           \
    X(xrCreateSession)                             \
    X(xrDestroySession)                            \
    X(xrBeginSession)                              \
    X(xrEndSession)                                \
    X(xrPollEvent)                                 \
    X(xrResultToString)                            \
    X(xrStringToPath)                              \
    X(xrPathToString)                              \
    X(xrCreateActionSet)                           \
    X(xrCreateAction)                              \
    X(xrSuggestInteractionProfileBindings)         \
    X(xrAttachSessionActionSets)                   \
    X(xrCreateActionSpace)                         \
    X(xrCreateReferenceSpace)                      \
    X(xrSyncActions)                               \
    X(xrGetActionStatePose)                        \
    X(xrGetCurrentInteractionProfile)              \
    X(xrLocateSpace)                               \
    X(xrConvertWin32PerformanceCounterToTimeKHR)

const char* StateName(XrSessionState s) {
    switch (s) {
        case XR_SESSION_STATE_IDLE: return "IDLE";
        case XR_SESSION_STATE_READY: return "READY";
        case XR_SESSION_STATE_SYNCHRONIZED: return "SYNCHRONIZED";
        case XR_SESSION_STATE_VISIBLE: return "VISIBLE";
        case XR_SESSION_STATE_FOCUSED: return "FOCUSED";
        case XR_SESSION_STATE_STOPPING: return "STOPPING";
        case XR_SESSION_STATE_LOSS_PENDING: return "LOSS_PENDING";
        case XR_SESSION_STATE_EXITING: return "EXITING";
        default: return "UNKNOWN";
    }
}

Vec3 Rotate(const XrQuaternionf& q, Vec3 v) {
    // v + 2 * cross(q.xyz, cross(q.xyz, v) + w * v)
    Vec3 u{q.x, q.y, q.z};
    Vec3 c{u.y * v.z - u.z * v.y + q.w * v.x, u.z * v.x - u.x * v.z + q.w * v.y,
           u.x * v.y - u.y * v.x + q.w * v.z};
    return {v.x + 2 * (u.y * c.z - u.z * c.y), v.y + 2 * (u.z * c.x - u.x * c.z),
            v.z + 2 * (u.x * c.y - u.y * c.x)};
}

}  // namespace

struct OpenXrSession::Impl {
    HMODULE loader = nullptr;
    PFN_xrGetInstanceProcAddr getProc = nullptr;
#define X(name) PFN_##name name = nullptr;
    XR_FUNCS(X)
#undef X

    XrInstance instance = XR_NULL_HANDLE;
    XrSystemId system = XR_NULL_SYSTEM_ID;
    XrSession session = XR_NULL_HANDLE;
    XrActionSet set = XR_NULL_HANDLE;
    XrAction action = XR_NULL_HANDLE;
    XrSpace gaze = XR_NULL_HANDLE;
    XrSpace view = XR_NULL_HANDLE;
    XrPath eyes_user = XR_NULL_PATH;
    XrSessionState state = XR_SESSION_STATE_UNKNOWN;
    bool running = false;

    std::string Str(XrResult r) {
        char buf[XR_MAX_RESULT_STRING_SIZE] = {};
        if (xrResultToString && instance) xrResultToString(instance, r, buf);
        else std::snprintf(buf, sizeof buf, "XrResult(%d)", r);
        return buf;
    }

    XrTime Now() {
        LARGE_INTEGER c;
        QueryPerformanceCounter(&c);
        XrTime t = 0;
        xrConvertWin32PerformanceCounterToTimeKHR(instance, &c, &t);
        return t;
    }

    void ReportProfile(Recorder& rec) {
        XrInteractionProfileState ps{XR_TYPE_INTERACTION_PROFILE_STATE};
        XrResult r = xrGetCurrentInteractionProfile(session, eyes_user, &ps);
        std::string name = "(none)";
        if (XR_SUCCEEDED(r) && ps.interactionProfile != XR_NULL_PATH) {
            char buf[XR_MAX_PATH_LENGTH] = {};
            uint32_t n = 0;
            xrPathToString(instance, ps.interactionProfile, sizeof buf, &n, buf);
            name = buf;
        }
        rec.Note("interaction profile for /user/eyes_ext: " + name + " (" + Str(r) + ")");
    }
};

bool OpenXrSession::Init(Recorder& rec) {
    impl_ = new Impl;
    Impl& x = *impl_;
    x.loader = LoadLibraryW(L"openxr_loader.dll");
    if (!x.loader) {
        x.loader = LoadLibraryW(
            L"C:\\Program Files (x86)\\Steam\\steamapps\\common\\SteamVR\\bin\\win64\\openxr_loader.dll");
    }
    if (!x.loader) {
        rec.Note("no OpenXR loader (openxr_loader.dll)");
        return false;
    }
    x.getProc = reinterpret_cast<PFN_xrGetInstanceProcAddr>(GetProcAddress(x.loader, "xrGetInstanceProcAddr"));
    PFN_xrEnumerateInstanceExtensionProperties enumExt = nullptr;
    PFN_xrCreateInstance createInstance = nullptr;
    x.getProc(XR_NULL_HANDLE, "xrEnumerateInstanceExtensionProperties",
              reinterpret_cast<PFN_xrVoidFunction*>(&enumExt));
    x.getProc(XR_NULL_HANDLE, "xrCreateInstance", reinterpret_cast<PFN_xrVoidFunction*>(&createInstance));

    uint32_t n = 0;
    XrResult r = enumExt(nullptr, 0, &n, nullptr);
    if (XR_FAILED(r)) {
        rec.Note("xrEnumerateInstanceExtensionProperties failed: " + std::to_string(r));
        return false;
    }
    std::vector<XrExtensionProperties> exts(n, {XR_TYPE_EXTENSION_PROPERTIES});
    enumExt(nullptr, n, &n, exts.data());
    auto has = [&](const char* name) {
        for (auto& e : exts) if (std::strcmp(e.extensionName, name) == 0) return true;
        return false;
    };
    const char* want[] = {XR_MND_HEADLESS_EXTENSION_NAME, XR_EXT_EYE_GAZE_INTERACTION_EXTENSION_NAME,
                          XR_KHR_WIN32_CONVERT_PERFORMANCE_COUNTER_TIME_EXTENSION_NAME};
    for (const char* w : want) {
        rec.Note(std::string("extension ") + w + (has(w) ? ": offered" : ": NOT offered"));
        if (!has(w)) return false;
    }

    XrInstanceCreateInfo ci{XR_TYPE_INSTANCE_CREATE_INFO};
    std::strcpy(ci.applicationInfo.applicationName, "rez-gaze probe");
    std::strcpy(ci.applicationInfo.engineName, "rez-gaze");
    ci.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 0);
    ci.enabledExtensionCount = 3;
    ci.enabledExtensionNames = want;
    r = createInstance(&ci, &x.instance);
    if (XR_FAILED(r)) {
        rec.Note("xrCreateInstance failed: " + std::to_string(r));
        return false;
    }
#define X(name) x.getProc(x.instance, #name, reinterpret_cast<PFN_xrVoidFunction*>(&x.name));
    XR_FUNCS(X)
#undef X

    XrInstanceProperties ip{XR_TYPE_INSTANCE_PROPERTIES};
    x.xrGetInstanceProperties(x.instance, &ip);
    rec.Note(std::string("runtime ") + ip.runtimeName + " " +
             std::to_string(XR_VERSION_MAJOR(ip.runtimeVersion)) + "." +
             std::to_string(XR_VERSION_MINOR(ip.runtimeVersion)) + "." +
             std::to_string(XR_VERSION_PATCH(ip.runtimeVersion)));

    XrSystemGetInfo gi{XR_TYPE_SYSTEM_GET_INFO};
    gi.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    r = x.xrGetSystem(x.instance, &gi, &x.system);
    if (XR_FAILED(r)) {
        rec.Note("xrGetSystem failed: " + x.Str(r) + " (headset connected?)");
        return false;
    }
    XrSystemEyeGazeInteractionPropertiesEXT gp{XR_TYPE_SYSTEM_EYE_GAZE_INTERACTION_PROPERTIES_EXT};
    XrSystemProperties sp{XR_TYPE_SYSTEM_PROPERTIES, &gp};
    x.xrGetSystemProperties(x.instance, x.system, &sp);
    rec.Note(std::string("system ") + sp.systemName + "; supportsEyeGazeInteraction = " +
             (gp.supportsEyeGazeInteraction ? "true" : "false") +
             (gp.supportsEyeGazeInteraction ? "" : " (Steam Link's 'send eye tracking data to other apps' off?)"));

    XrActionSetCreateInfo asci{XR_TYPE_ACTION_SET_CREATE_INFO};
    std::strcpy(asci.actionSetName, "gaze");
    std::strcpy(asci.localizedActionSetName, "Gaze");
    x.xrCreateActionSet(x.instance, &asci, &x.set);
    XrActionCreateInfo aci{XR_TYPE_ACTION_CREATE_INFO};
    aci.actionType = XR_ACTION_TYPE_POSE_INPUT;
    std::strcpy(aci.actionName, "gaze_pose");
    std::strcpy(aci.localizedActionName, "Gaze pose");
    x.xrCreateAction(x.set, &aci, &x.action);

    XrPath profile = XR_NULL_PATH, binding = XR_NULL_PATH;
    x.xrStringToPath(x.instance, "/interaction_profiles/ext/eye_gaze_interaction", &profile);
    x.xrStringToPath(x.instance, "/user/eyes_ext/input/gaze_ext/pose", &binding);
    x.xrStringToPath(x.instance, "/user/eyes_ext", &x.eyes_user);
    XrActionSuggestedBinding sb{x.action, binding};
    XrInteractionProfileSuggestedBinding isb{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
    isb.interactionProfile = profile;
    isb.countSuggestedBindings = 1;
    isb.suggestedBindings = &sb;
    r = x.xrSuggestInteractionProfileBindings(x.instance, &isb);
    if (XR_FAILED(r)) {
        rec.Note("suggesting the eye gaze binding failed: " + x.Str(r));
        return false;
    }

    // XR_MND_headless: no graphics binding in the chain.
    XrSessionCreateInfo sci{XR_TYPE_SESSION_CREATE_INFO};
    sci.systemId = x.system;
    r = x.xrCreateSession(x.instance, &sci, &x.session);
    if (XR_FAILED(r)) {
        rec.Note("headless xrCreateSession failed: " + x.Str(r));
        return false;
    }
    XrSessionActionSetsAttachInfo ai{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
    ai.countActionSets = 1;
    ai.actionSets = &x.set;
    x.xrAttachSessionActionSets(x.session, &ai);

    XrActionSpaceCreateInfo spci{XR_TYPE_ACTION_SPACE_CREATE_INFO};
    spci.action = x.action;
    spci.poseInActionSpace.orientation.w = 1;
    x.xrCreateActionSpace(x.session, &spci, &x.gaze);
    XrReferenceSpaceCreateInfo rsci{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    rsci.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
    rsci.poseInReferenceSpace.orientation.w = 1;
    x.xrCreateReferenceSpace(x.session, &rsci, &x.view);
    rec.Note("headless session created; waiting for READY");
    return true;
}

void OpenXrSession::Poll(Recorder& rec) {
    Impl& x = *impl_;
    XrEventDataBuffer ev{XR_TYPE_EVENT_DATA_BUFFER};
    while (x.xrPollEvent(x.instance, &ev) == XR_SUCCESS) {
        if (ev.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
            auto& e = reinterpret_cast<XrEventDataSessionStateChanged&>(ev);
            x.state = e.state;
            rec.Note(std::string("session state ") + StateName(e.state));
            if (e.state == XR_SESSION_STATE_READY) {
                XrSessionBeginInfo bi{XR_TYPE_SESSION_BEGIN_INFO};
                bi.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                XrResult r = x.xrBeginSession(x.session, &bi);
                rec.Note("xrBeginSession: " + x.Str(r));
                x.running = XR_SUCCEEDED(r);
            } else if (e.state == XR_SESSION_STATE_STOPPING) {
                x.xrEndSession(x.session);
                x.running = false;
            } else if (e.state == XR_SESSION_STATE_EXITING || e.state == XR_SESSION_STATE_LOSS_PENDING) {
                g_stop = true;
            }
        } else if (ev.type == XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED) {
            x.ReportProfile(rec);
        } else if (ev.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING) {
            rec.Note("instance loss pending");
            g_stop = true;
        }
        ev = {XR_TYPE_EVENT_DATA_BUFFER};
    }
    if (!x.running) return;

    XrActiveActionSet active{x.set, XR_NULL_PATH};
    XrActionsSyncInfo si{XR_TYPE_ACTIONS_SYNC_INFO};
    si.countActiveActionSets = 1;
    si.activeActionSets = &active;
    XrResult sr = x.xrSyncActions(x.session, &si);

    XrActionStateGetInfo gi{XR_TYPE_ACTION_STATE_GET_INFO};
    gi.action = x.action;
    XrActionStatePose ps{XR_TYPE_ACTION_STATE_POSE};
    x.xrGetActionStatePose(x.session, &gi, &ps);

    XrSpaceLocation loc{XR_TYPE_SPACE_LOCATION};
    XrResult lr = x.xrLocateSpace(x.gaze, x.view, x.Now(), &loc);

    Sample s;
    s.result = std::string(StateName(x.state)) + ":" + x.Str(sr);
    if (XR_FAILED(lr)) s.result += "/locate:" + x.Str(lr);
    s.active = ps.isActive;
    s.valid = XR_SUCCEEDED(lr) && (loc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT);
    s.tracked = s.valid && (loc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT);
    if (s.valid) {
        // Located in VIEW space, so this is already head-relative.
        s.dir_head = Rotate(loc.pose.orientation, {0, 0, -1});
        s.origin_head = {loc.pose.position.x, loc.pose.position.y, loc.pose.position.z};
    }
    rec.Add(s);
}

void OpenXrSession::Shutdown() {
    if (!impl_) return;
    Impl& x = *impl_;
    if (x.session) {
        if (x.running) x.xrEndSession(x.session);
        x.xrDestroySession(x.session);
    }
    if (x.instance) x.xrDestroyInstance(x.instance);
    if (x.loader) FreeLibrary(x.loader);
    delete impl_;
    impl_ = nullptr;
}
