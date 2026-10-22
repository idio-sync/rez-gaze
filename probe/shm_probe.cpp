// shm mode: prints the helper's shared-memory block for a game pid once a second.
#include "probes.h"

#include "../shared/gaze_shm.h"

int RunShmProbe(unsigned long pid, double seconds, Recorder& rec) {
    rezgaze::GazeShmReader r;
    std::wstring name = rezgaze::ShmName(pid);
    uint64_t last_count = 0;
    bool opened = false;
    while (!g_stop && (seconds <= 0 || rec.NowMs() < seconds * 1000)) {
        if (!opened) opened = r.Open(name);
        rezgaze::GazeSampleShm s{};
        std::string status;
        if (!opened) {
            rec.Note("no block Local\\rez_gaze_" + std::to_string(pid) + " yet");
        } else if (!r.Read(s, &status)) {
            rec.Note("block present but unreadable (bad magic/version, or mid-write)");
        } else {
            LARGE_INTEGER now, f;
            QueryPerformanceCounter(&now);
            QueryPerformanceFrequency(&f);
            double age_ms = s.qpc ? (now.QuadPart - s.qpc) * 1000.0 / f.QuadPart : -1;
            char line[256];
            std::snprintf(line, sizeof line,
                          "status \"%s\" | count %llu (+%llu/s) | flags %s%s%s | dir %.3f %.3f %.3f | age %.0f ms",
                          status.c_str(), s.count, s.count - last_count, s.flags & 1 ? "active " : "",
                          s.flags & 2 ? "valid " : "", s.flags & 4 ? "tracked" : "", s.dir[0], s.dir[1], s.dir[2],
                          age_ms);
            rec.Note(line);
            last_count = s.count;
        }
        Sleep(1000);
    }
    return opened ? 0 : 1;
}
