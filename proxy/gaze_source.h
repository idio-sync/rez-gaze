// Where the stand-in DLL gets the gaze: the helper (OpenVR eyetracking action, shared memory),
// Steam Link's OSC stream (PC only), or neither (aim straight ahead). See the design's
// "Gaze source" section.
#pragma once

#include "aim.h"

#include <windows.h>

#include <atomic>
#include <mutex>
#include <string>

namespace rezgaze {

enum class SourceMode { Auto, Helper, Osc };
enum class Source { Helper, Osc, Head };

constexpr double kFreshMs = 250;

// Ages in ms, or < 0 for "never". A helper sample counts only if tracked.
Source ChooseSource(SourceMode mode, double helper_age_ms, bool helper_tracked, double osc_age_ms);

const char* SourceName(Source s);

using LogFn = void (*)(const char* fmt, ...);

// Starts rez_gaze_helper.exe once per process and reads its block.
class HelperSource {
public:
    // False if CreateProcess failed; the error is logged.
    bool Start(const std::wstring& helper_exe, DWORD game_pid, LogFn log);
    // False until the helper's block exists and reads consistently.
    bool Read(V3& dir, double& age_ms, bool& tracked, std::string& status);

private:
    std::once_flag once_;
    bool started_ = false;
    DWORD game_pid_ = 0;
    struct Reader;
    Reader* reader_ = nullptr;
};

// Listens for /tracking/eye/CenterVecFull on 127.0.0.1:port on its own thread.
class OscSource {
public:
    void Start(int port, LogFn log);
    // False if no packet has arrived yet.
    bool Read(V3& dir, double& age_ms);
    long TakePacketCount() { return packets_.exchange(0); }

    void HandlePacket(const unsigned char* p, size_t n);  // public for tests

private:
    std::mutex mutex_;
    V3 dir_{0, 0, -1};
    LONGLONG qpc_ = 0;
    std::atomic<long> packets_{0};
};

}  // namespace rezgaze
