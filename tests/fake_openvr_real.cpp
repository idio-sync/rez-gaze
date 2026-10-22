// Test stand-in for Rez's openvr_api_real.dll: exports every name the stand-in forwards, and
// VR_GetGenericInterface returns a dummy object (a vtable of no-op slots) for any version, so
// the stand-in's interface patching and helper start run without SteamVR.
#include <windows.h>

#include <cstdint>

namespace {

uint64_t __fastcall NoOp() { return 0; }

void* g_vtable[64];
struct Dummy {
    void** vtable;
} g_dummy{g_vtable};

}  // namespace

extern "C" {
__declspec(dllexport) void* __cdecl VR_GetGenericInterface(const char*, int* err) {
    for (void*& slot : g_vtable)
        if (!slot) slot = reinterpret_cast<void*>(&NoOp);
    if (err) *err = 0;
    return &g_dummy;
}
__declspec(dllexport) uint32_t __cdecl VR_GetInitToken() { return 1; }
__declspec(dllexport) const char* __cdecl VR_GetStringForHmdError(int) { return "fake"; }
__declspec(dllexport) const char* __cdecl VR_GetVRInitErrorAsEnglishDescription(int) { return "fake"; }
__declspec(dllexport) const char* __cdecl VR_GetVRInitErrorAsSymbol(int) { return "fake"; }
__declspec(dllexport) uint32_t __cdecl VR_InitInternal(int* err, int) {
    if (err) *err = 0;
    return 1;
}
__declspec(dllexport) bool __cdecl VR_IsHmdPresent() { return true; }
__declspec(dllexport) bool __cdecl VR_IsInterfaceVersionValid(const char*) { return true; }
__declspec(dllexport) bool __cdecl VR_IsRuntimeInstalled() { return true; }
__declspec(dllexport) const char* __cdecl VR_RuntimePath() { return "fake"; }
__declspec(dllexport) void __cdecl VR_ShutdownInternal() {}
}
