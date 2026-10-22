#include "test_main.h"

#include "../shared/gaze_shm.h"

#include <atomic>
#include <string>
#include <thread>

using namespace rezgaze;

namespace {

// A fresh name per test, so tests never see each other's blocks.
std::wstring TestName(const wchar_t* tag) {
    return L"Local\\rez_gaze_test_" + std::wstring(tag) + L"_" + std::to_wstring(GetCurrentProcessId());
}

// A second view of a writer's block, for tests that corrupt it on purpose.
struct RawView {
    HANDLE map = nullptr;
    GazeShmBlock* block = nullptr;
    explicit RawView(const std::wstring& name) {
        map = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, name.c_str());
        if (map) block = static_cast<GazeShmBlock*>(MapViewOfFile(map, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(GazeShmBlock)));
    }
    ~RawView() {
        if (block) UnmapViewOfFile(block);
        if (map) CloseHandle(map);
    }
};

}  // namespace

TEST(shm_name) { EXPECT(ShmName(1234) == L"Local\\rez_gaze_1234"); }

TEST(shm_roundtrip) {
    std::wstring name = TestName(L"roundtrip");
    GazeShmWriter w;
    EXPECT(w.Create(name));
    GazeSampleShm s{{0.1f, 0.2f, -0.97f}, 7, 123, 5};
    w.Publish(s);
    GazeShmReader r;
    EXPECT(r.Open(name));
    GazeSampleShm out{};
    EXPECT(r.Read(out, nullptr));
    EXPECT(out.dir[0] == 0.1f && out.dir[1] == 0.2f && out.dir[2] == -0.97f);
    EXPECT(out.flags == 7 && out.qpc == 123 && out.count == 5);
}

TEST(shm_status) {
    std::wstring name = TestName(L"status");
    GazeShmWriter w;
    EXPECT(w.Create(name));
    w.SetStatus("vr init failed");
    GazeShmReader r;
    EXPECT(r.Open(name));
    GazeSampleShm out{};
    std::string status;
    EXPECT(r.Read(out, &status));
    EXPECT(status == "vr init failed");
}

TEST(shm_reader_before_writer) {
    GazeShmReader r;
    EXPECT(!r.Open(TestName(L"nobody")));
    GazeSampleShm out{};
    EXPECT(!r.Read(out, nullptr));
}

TEST(shm_reader_created_block_outlives_writer) {
    // The DLL creates the block before starting the helper, so a helper that fails and exits
    // still leaves its status behind.
    std::wstring name = TestName(L"outlive");
    GazeShmReader r;
    EXPECT(r.Create(name));
    GazeSampleShm out{};
    EXPECT(!r.Read(out, nullptr));  // no writer yet: magic not set
    {
        GazeShmWriter w;
        EXPECT(w.Create(name));
        w.SetStatus("vr init failed: no server");
    }
    std::string status;
    EXPECT(r.Read(out, &status));
    EXPECT(status == "vr init failed: no server");
}

TEST(shm_bad_magic) {
    std::wstring name = TestName(L"magic");
    GazeShmWriter w;
    EXPECT(w.Create(name));
    RawView raw(name);
    EXPECT(raw.block != nullptr);
    if (raw.block) raw.block->magic = 0;
    GazeShmReader r;
    EXPECT(r.Open(name));
    GazeSampleShm out{};
    EXPECT(!r.Read(out, nullptr));
}

TEST(shm_odd_seq_is_torn) {
    std::wstring name = TestName(L"torn");
    GazeShmWriter w;
    EXPECT(w.Create(name));
    w.Publish(GazeSampleShm{{0, 0, -1}, 7, 1, 1});
    RawView raw(name);
    EXPECT(raw.block != nullptr);
    if (raw.block) raw.block->seq |= 1;
    GazeShmReader r;
    EXPECT(r.Open(name));
    GazeSampleShm out{};
    EXPECT(!r.Read(out, nullptr));
}

TEST(shm_hammer) {
    std::wstring name = TestName(L"hammer");
    GazeShmWriter w;
    EXPECT(w.Create(name));
    GazeShmReader r;
    EXPECT(r.Open(name));
    std::atomic<bool> done{false};
    std::thread writer([&] {
        for (uint64_t i = 1; i <= 200000; ++i) {
            float f = static_cast<float>(i);
            w.Publish(GazeSampleShm{{f, f, f}, 7, static_cast<int64_t>(i), i});
        }
        done = true;
    });
    long good = 0, torn = 0;
    while (!done) {
        GazeSampleShm out{};
        if (!r.Read(out, nullptr)) continue;
        ++good;
        float c = static_cast<float>(out.count);
        if (!(out.dir[0] == c && out.dir[1] == c && out.dir[2] == c && out.qpc == static_cast<int64_t>(out.count))) ++torn;
    }
    writer.join();
    EXPECT(good > 0);
    EXPECT(torn == 0);
}
