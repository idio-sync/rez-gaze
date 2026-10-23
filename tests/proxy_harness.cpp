// Loads the stand-in openvr_api.dll from its own folder (with tests/fake_openvr_real.cpp as
// openvr_api_real.dll), asks for IVRSystem_015 twice, and checks what the stand-in did:
//   default:            exactly one rez_gaze_helper.exe child, whose block reports a status
//   --helper-missing:   no child, and the stand-in's log says it couldn't start the helper
// Prints PASS/FAIL and returns 0/1.
#include "../shared/gaze_shm.h"

#include <tlhelp32.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

namespace {

int CountHelperChildren() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32W pe{sizeof pe};
    int n = 0;
    for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe)) {
        if (pe.th32ParentProcessID == GetCurrentProcessId() && _wcsicmp(pe.szExeFile, L"rez_gaze_helper.exe") == 0) ++n;
    }
    CloseHandle(snap);
    return n;
}

std::string ReadFile(const std::wstring& path) {
    std::ifstream f(path);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

}  // namespace

int main(int argc, char** argv) {
    bool missing = argc > 1 && std::strcmp(argv[1], "--helper-missing") == 0;
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring dir(buf);
    dir.resize(dir.find_last_of(L'\\') + 1);

    HMODULE standin = LoadLibraryW((dir + L"openvr_api.dll").c_str());
    if (!standin) {
        std::printf("FAIL: can't load the stand-in (error %lu)\n", GetLastError());
        return 1;
    }
    using FnGet = void*(__cdecl*)(const char*, int*);
    auto get = reinterpret_cast<FnGet>(GetProcAddress(standin, "VR_GetGenericInterface"));
    int err = 0;
    void* a = get("IVRSystem_015", &err);
    void* b = get("IVRSystem_015", &err);
    if (!a || a != b) {
        std::printf("FAIL: VR_GetGenericInterface returned %p, %p\n", a, b);
        return 1;
    }
    // The stand-in starts the helper from its WaitGetPoses hook (compositor slot 2), after
    // helper_delay_s (0 in the harness ini): call it as the game would, several times.
    void* compositor = get("IVRCompositor_015", &err);
    using FnWait = int(__fastcall*)(void*, void*, uint32_t, void*, uint32_t);
    auto wait = reinterpret_cast<FnWait>((*reinterpret_cast<void***>(compositor))[2]);
    for (int i = 0; i < 5; ++i) wait(compositor, nullptr, 0, nullptr, 0);
    Sleep(2000);
    int children = CountHelperChildren();

    if (missing) {
        std::string log = ReadFile(dir + L"rez_gaze.log");
        bool logged = log.find("helper: can't start") != std::string::npos;
        std::printf("%s: %d helper children, stand-in log %s \"helper: can't start\"\n",
                    children == 0 && logged ? "PASS" : "FAIL", children, logged ? "has" : "lacks");
        return children == 0 && logged ? 0 : 1;
    }

    // Count starts from the stand-in's log: a helper whose VR init failed (SteamVR off) has
    // already exited, so live children alone can't show it was started once.
    std::string log = ReadFile(dir + L"rez_gaze.log");
    int starts = 0;
    for (size_t at = log.find("helper: started pid"); at != std::string::npos; at = log.find("helper: started pid", at + 1))
        ++starts;
    // VR init can take seconds (SteamVR up, headset not connected: ~4 s to be refused), so wait
    // up to 10 s for the status to leave "starting".
    rezgaze::GazeShmReader r;
    rezgaze::GazeSampleShm s{};
    std::string status;
    bool read = false;
    for (int i = 0; i < 100; ++i) {
        read = r.Open(rezgaze::ShmName(GetCurrentProcessId())) && r.Read(s, &status);
        if (read && status != "starting") break;
        Sleep(100);
    }
    bool status_ok = read && (status == "running" || status.rfind("vr init failed", 0) == 0);
    bool ok = starts == 1 && children <= 1 && status_ok;
    std::printf("%s: %d start(s) logged, %d helper children alive after 2 s, status \"%s\"\n", ok ? "PASS" : "FAIL",
                starts, children, read ? status.c_str() : "(no block)");
    return ok ? 0 : 1;
}
