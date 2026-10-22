// Shared pieces of the gaze probe: head-space math and the recorder that prints a status line
// a second, writes one CSV row per poll and sums the run up at the end.
#pragma once

#include <atomic>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

extern std::atomic<bool> g_stop;

struct Vec3 {
    double x = 0, y = 0, z = 0;
};

inline Vec3 Sub(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline double Length(Vec3 v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }
inline Vec3 Normalize(Vec3 v) {
    double l = Length(v);
    return l > 0 ? Vec3{v.x / l, v.y / l, v.z / l} : Vec3{0, 0, -1};
}

constexpr double kRadToDeg = 57.29577951308232;

// OpenVR and OpenXR share the convention: +X right, +Y up, -Z forward.
inline double YawDeg(Vec3 d) { return std::atan2(d.x, -d.z) * kRadToDeg; }
inline double PitchDeg(Vec3 d) { return std::atan2(d.y, std::sqrt(d.x * d.x + d.z * d.z)) * kRadToDeg; }
inline double AngleToForwardDeg(Vec3 d) {
    double c = -Normalize(d).z;
    return std::acos(c < -1 ? -1 : c > 1 ? 1 : c) * kRadToDeg;
}

struct Sample {
    std::string result;     // the API's result for this poll, e.g. "ok" or an error name
    bool active = false;    // the runtime says the action is bound and active
    bool valid = false;     // a direction came back
    bool tracked = false;   // the runtime says the eyes are tracked
    Vec3 dir_head;          // gaze direction in head space
    Vec3 origin_head;       // gaze origin in head space (metres), when the API gives one
};

class Recorder {
public:
    explicit Recorder(const std::string& mode);
    ~Recorder();

    // Milliseconds since the recorder started.
    double NowMs() const;
    void Add(const Sample& s);
    void Note(const std::string& line);  // timestamped line on the console and in the CSV's log
    void Summary();
    const std::string& CsvPath() const { return csv_path_; }

private:
    void PrintSecond();

    std::string tag_;
    std::string csv_path_;
    FILE* csv_ = nullptr;
    long long start_qpc_ = 0;
    long long qpc_freq_ = 1;

    // This second.
    double second_start_ms_ = 0;
    int polls_ = 0, valid_ = 0, tracked_ = 0, fresh_ = 0;
    std::map<std::string, int> results_;
    Sample last_;
    bool have_last_ = false;

    // The whole run.
    long long total_polls_ = 0, total_valid_ = 0, total_tracked_ = 0, total_fresh_ = 0;
    double first_tracked_ms_ = -1;
    std::map<std::string, long long> total_results_;
    std::vector<double> angles_;
    Vec3 prev_dir_;
    bool have_prev_ = false;
};
