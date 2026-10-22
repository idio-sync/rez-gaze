#include "gaze_source.h"

#include "../shared/gaze_shm.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <cstdio>
#include <cstring>
#include <thread>

namespace rezgaze {

namespace {

LONGLONG Qpc() {
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return c.QuadPart;
}

double AgeMs(LONGLONG then) {
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    return (Qpc() - then) * 1000.0 / f.QuadPart;
}

bool Fresh(double age_ms) { return age_ms >= 0 && age_ms <= kFreshMs; }

float BeFloat(const unsigned char* p) {
    uint32_t u = (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
    float f;
    std::memcpy(&f, &u, 4);
    return f;
}

}  // namespace

Source ChooseSource(SourceMode mode, double helper_age_ms, bool helper_tracked, double osc_age_ms) {
    bool helper = Fresh(helper_age_ms) && helper_tracked;
    bool osc = Fresh(osc_age_ms);
    switch (mode) {
        case SourceMode::Helper: return helper ? Source::Helper : Source::Head;
        case SourceMode::Osc: return osc ? Source::Osc : Source::Head;
        default: return helper ? Source::Helper : osc ? Source::Osc : Source::Head;
    }
}

const char* SourceName(Source s) { return s == Source::Helper ? "helper" : s == Source::Osc ? "osc" : "head"; }

// ---- helper --------------------------------------------------------------------------------

struct HelperSource::Reader {
    GazeShmReader shm;
};

bool HelperSource::Start(const std::wstring& helper_exe, DWORD game_pid, LogFn log) {
    std::call_once(once_, [&] {
        game_pid_ = game_pid;
        reader_ = new Reader;
        // Create the block first: it then outlives a helper that fails and exits, so its status
        // still reaches the log.
        if (!reader_->shm.Create(ShmName(game_pid))) log("helper: can't create shared memory (error %lu)", GetLastError());
        std::wstring cmd = L"\"" + helper_exe + L"\" --parent " + std::to_wstring(game_pid);
        STARTUPINFOW si{sizeof si};
        PROCESS_INFORMATION pi{};
        if (CreateProcessW(helper_exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                           nullptr, &si, &pi)) {
            started_ = true;
            log("helper: started pid %lu", pi.dwProcessId);
            CloseHandle(pi.hThread);
            process_ = pi.hProcess;  // kept, so a helper that dies at load still leaves its exit code
        } else {
            log("helper: can't start %ls (error %lu); gaze falls back to OSC, then head", helper_exe.c_str(),
                GetLastError());
        }
    });
    return started_;
}

bool HelperSource::Read(V3& dir, double& age_ms, bool& tracked, std::string& status) {
    if (!reader_ || !reader_->shm.Open(ShmName(game_pid_))) return false;
    GazeSampleShm s{};
    if (!reader_->shm.Read(s, &status)) return false;
    dir = {s.dir[0], s.dir[1], s.dir[2]};
    age_ms = s.qpc ? AgeMs(s.qpc) : -1;
    tracked = (s.flags & kGazeTracked) != 0;
    return true;
}

bool HelperSource::ExitCode(DWORD& code) {
    if (!process_ || WaitForSingleObject(process_, 0) != WAIT_OBJECT_0) return false;
    return GetExitCodeProcess(process_, &code) != 0;
}

std::string HelperExitText(DWORD code) {
    switch (code) {
        case 0: return "0 (normal exit)";
        case 2: return "2 (bad arguments)";
        case 3: return "3 (VR init or action manifest failed; see its status)";
        case 4: return "4 (couldn't create shared memory)";
        case 0xC0000135: return "0xC0000135 (a DLL is missing: rez_gaze\\openvr_api.dll?)";
        case 0xC0000005: return "0xC0000005 (crashed: access violation)";
        default: {
            char buf[32];
            std::snprintf(buf, sizeof buf, "0x%08lX", code);
            return buf;
        }
    }
}

// ---- OSC -----------------------------------------------------------------------------------

void OscSource::Start(int port, LogFn log) {
    std::thread([this, port, log] {
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
        SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(static_cast<u_short>(port));
        inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
        if (bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0) {
            log("OSC: can't bind 127.0.0.1:%d (error %d)", port, WSAGetLastError());
            closesocket(s);
            return;
        }
        log("OSC: listening on 127.0.0.1:%d", port);
        unsigned char buf[4096];
        for (;;) {
            int n = recv(s, reinterpret_cast<char*>(buf), sizeof buf, 0);
            if (n > 0) HandlePacket(buf, static_cast<size_t>(n));
        }
    }).detach();
}

bool OscSource::Read(V3& dir, double& age_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (qpc_ == 0) return false;
    dir = dir_;
    age_ms = AgeMs(qpc_);
    return true;
}

// Finds /tracking/eye/CenterVecFull ,fff in a packet (plain message or bundle).
void OscSource::HandlePacket(const unsigned char* p, size_t n) {
    if (n >= 16 && std::memcmp(p, "#bundle", 8) == 0) {
        size_t off = 16;
        while (off + 4 <= n) {
            uint32_t len = (uint32_t(p[off]) << 24) | (uint32_t(p[off + 1]) << 16) | (uint32_t(p[off + 2]) << 8) |
                           p[off + 3];
            off += 4;
            if (off + len > n) return;
            HandlePacket(p + off, len);
            off += len;
        }
        return;
    }
    static const char kAddr[] = "/tracking/eye/CenterVecFull";  // 27 chars, padded to 28
    if (n < 28 + 8 + 12 || std::memcmp(p, kAddr, sizeof kAddr) != 0) return;
    if (std::memcmp(p + 28, ",fff", 4) != 0) return;
    const unsigned char* a = p + 28 + 8;
    // VRChat's convention is +Z forward.
    V3 g = Norm({BeFloat(a), BeFloat(a + 4), -BeFloat(a + 8)});
    std::lock_guard<std::mutex> lock(mutex_);
    dir_ = g;
    qpc_ = Qpc();
    ++packets_;
}

}  // namespace rezgaze
