// The gaze block the helper publishes and the stand-in DLL reads: named shared memory
// Local\rez_gaze_<game pid>, one writer, written under a sequence lock (seq is odd while a
// write is in progress). Gaze is head-space, -Z forward, unit length.
#pragma once

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <string>

namespace rezgaze {

constexpr uint32_t kShmMagic = 0x5A47525A;  // "ZRGZ"
constexpr uint32_t kShmVersion = 1;

enum GazeFlags : uint32_t { kGazeActive = 1, kGazeValid = 2, kGazeTracked = 4 };

struct GazeSampleShm {
    float dir[3];
    uint32_t flags;
    int64_t qpc;     // QueryPerformanceCounter when sampled
    uint64_t count;  // samples published so far
};

struct GazeShmBlock {
    uint32_t magic, version;
    volatile LONG seq;
    uint32_t helper_pid;
    GazeSampleShm sample;
    char status[128];
};

inline std::wstring ShmName(DWORD game_pid) { return L"Local\\rez_gaze_" + std::to_wstring(game_pid); }

class GazeShmWriter {
public:
    bool Create(const std::wstring& name) {
        map_ = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(GazeShmBlock), name.c_str());
        if (!map_) return false;
        block_ = static_cast<GazeShmBlock*>(MapViewOfFile(map_, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(GazeShmBlock)));
        if (!block_) return false;
        Begin();
        block_->version = kShmVersion;
        block_->helper_pid = GetCurrentProcessId();
        block_->magic = kShmMagic;
        End();
        return true;
    }

    void Publish(const GazeSampleShm& s) {
        if (!block_) return;
        Begin();
        block_->sample = s;
        End();
    }

    void SetStatus(const char* text) {
        if (!block_) return;
        Begin();
        strncpy_s(block_->status, text, _TRUNCATE);
        End();
    }

    ~GazeShmWriter() {
        if (block_) UnmapViewOfFile(block_);
        if (map_) CloseHandle(map_);
    }

private:
    void Begin() {
        InterlockedIncrement(&block_->seq);  // odd: write in progress
        MemoryBarrier();
    }
    void End() {
        MemoryBarrier();
        InterlockedIncrement(&block_->seq);  // even: consistent
    }

    HANDLE map_ = nullptr;
    GazeShmBlock* block_ = nullptr;
};

class GazeShmReader {
public:
    bool Open(const std::wstring& name) {
        if (block_) return true;
        map_ = OpenFileMappingW(FILE_MAP_READ, FALSE, name.c_str());
        if (!map_) return false;
        block_ = static_cast<const GazeShmBlock*>(MapViewOfFile(map_, FILE_MAP_READ, 0, 0, sizeof(GazeShmBlock)));
        if (!block_) {
            CloseHandle(map_);
            map_ = nullptr;
            return false;
        }
        return true;
    }

    // A consistent snapshot of the sample (and status), or false if the block isn't open, isn't
    // ours, or stayed mid-write for 8 attempts.
    bool Read(GazeSampleShm& out, std::string* status) const {
        if (!block_) return false;
        for (int attempt = 0; attempt < 8; ++attempt) {
            LONG before = block_->seq;
            if (before & 1) {
                YieldProcessor();
                continue;
            }
            MemoryBarrier();
            GazeShmBlock copy;
            std::memcpy(&copy, const_cast<const void*>(static_cast<const volatile void*>(block_)), sizeof copy);
            MemoryBarrier();
            if (block_->seq != before) continue;
            if (copy.magic != kShmMagic || copy.version != kShmVersion) return false;
            out = copy.sample;
            if (status) {
                copy.status[sizeof copy.status - 1] = '\0';
                *status = copy.status;
            }
            return true;
        }
        return false;
    }

    ~GazeShmReader() {
        if (block_) UnmapViewOfFile(block_);
        if (map_) CloseHandle(map_);
    }

private:
    HANDLE map_ = nullptr;
    const GazeShmBlock* block_ = nullptr;
};

}  // namespace rezgaze
