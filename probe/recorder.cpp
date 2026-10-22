#include "common.h"

#include <windows.h>

#include <algorithm>
#include <ctime>
#include <filesystem>

namespace {

std::filesystem::path ExeDir() {
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return std::filesystem::path(std::wstring(buf, n)).parent_path();
}

double Percentile(std::vector<double> v, double p) {
    if (v.empty()) return NAN;
    std::sort(v.begin(), v.end());
    size_t i = static_cast<size_t>(p * (v.size() - 1) + 0.5);
    return v[std::min(i, v.size() - 1)];
}

}  // namespace

Recorder::Recorder(const std::string& mode) : tag_(mode) {
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    qpc_freq_ = f.QuadPart;
    start_qpc_ = c.QuadPart;

    std::filesystem::path dir = ExeDir() / "logs";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    char stamp[32];
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_s(&tm, &t);
    std::strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", &tm);
    csv_path_ = (dir / ("gaze-" + mode + "-" + stamp + ".csv")).string();
    csv_ = std::fopen(csv_path_.c_str(), "w");
    if (csv_) {
        std::fprintf(csv_, "t_ms,result,active,valid,tracked,fresh,yaw_deg,pitch_deg,dx,dy,dz,ox,oy,oz\n");
    }
}

Recorder::~Recorder() {
    if (csv_) std::fclose(csv_);
}

double Recorder::NowMs() const {
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return (c.QuadPart - start_qpc_) * 1000.0 / qpc_freq_;
}

void Recorder::Note(const std::string& line) {
    std::printf("[%7.1f s] %s: %s\n", NowMs() / 1000.0, tag_.c_str(), line.c_str());
    std::fflush(stdout);
}

void Recorder::Add(const Sample& s) {
    double t = NowMs();
    if (t - second_start_ms_ >= 1000.0) {
        if (polls_ > 0) PrintSecond();
        second_start_ms_ = t;
    }

    // A "fresh" sample is a direction that differs from the previous poll's: the count per
    // second approximates the tracker's update rate, since we poll faster than it updates.
    bool fresh = false;
    if (s.valid) {
        fresh = !have_prev_ || s.dir_head.x != prev_dir_.x || s.dir_head.y != prev_dir_.y ||
                s.dir_head.z != prev_dir_.z;
        prev_dir_ = s.dir_head;
        have_prev_ = true;
    }

    ++polls_;
    ++total_polls_;
    ++results_[s.result];
    ++total_results_[s.result];
    if (s.valid) {
        ++valid_;
        ++total_valid_;
    }
    if (s.tracked) {
        ++tracked_;
        ++total_tracked_;
        if (first_tracked_ms_ < 0) first_tracked_ms_ = t;
    }
    if (fresh) {
        ++fresh_;
        ++total_fresh_;
        if (s.tracked) angles_.push_back(AngleToForwardDeg(s.dir_head));
    }
    last_ = s;
    have_last_ = true;

    if (csv_) {
        std::fprintf(csv_, "%.1f,%s,%d,%d,%d,%d,%.3f,%.3f,%.5f,%.5f,%.5f,%.4f,%.4f,%.4f\n", t,
                     s.result.c_str(), s.active, s.valid, s.tracked, fresh,
                     s.valid ? YawDeg(s.dir_head) : NAN, s.valid ? PitchDeg(s.dir_head) : NAN,
                     s.dir_head.x, s.dir_head.y, s.dir_head.z, s.origin_head.x, s.origin_head.y,
                     s.origin_head.z);
    }
}

void Recorder::PrintSecond() {
    std::string res;
    for (auto& [name, n] : results_) {
        if (!res.empty()) res += ", ";
        res += name + " x" + std::to_string(n);
    }
    char gaze[96] = "no gaze";
    if (have_last_ && last_.valid) {
        std::snprintf(gaze, sizeof gaze, "yaw %+6.1f pitch %+6.1f (%4.1f off centre)",
                      YawDeg(last_.dir_head), PitchDeg(last_.dir_head),
                      AngleToForwardDeg(last_.dir_head));
    }
    std::printf("[%7.1f s] %s: polls %3d  active %d  valid %3d  tracked %3d  new %3d/s  %s  | %s\n",
                second_start_ms_ / 1000.0, tag_.c_str(), polls_, have_last_ && last_.active, valid_, tracked_,
                fresh_, gaze, res.c_str());
    std::fflush(stdout);
    polls_ = valid_ = tracked_ = fresh_ = 0;
    results_.clear();
}

void Recorder::Summary() {
    if (polls_ > 0) PrintSecond();
    double secs = NowMs() / 1000.0;
    std::printf("\n== %s summary: %.1f s, %lld polls\n", tag_.c_str(), secs, total_polls_);
    for (auto& [name, n] : total_results_) std::printf("   result %-40s %lld\n", name.c_str(), n);
    std::printf("   valid %lld (%.1f%%), tracked %lld (%.1f%%)\n", total_valid_,
                total_polls_ ? 100.0 * total_valid_ / total_polls_ : 0.0, total_tracked_,
                total_polls_ ? 100.0 * total_tracked_ / total_polls_ : 0.0);
    if (first_tracked_ms_ >= 0) {
        std::printf("   first tracked sample at %.1f s\n", first_tracked_ms_ / 1000.0);
    }
    std::printf("   new directions %lld (%.1f/s over the run)\n", total_fresh_,
                secs > 0 ? total_fresh_ / secs : 0.0);
    if (!angles_.empty()) {
        std::printf("   angle off head-forward: median %.1f, p90 %.1f, max %.1f deg\n",
                    Percentile(angles_, 0.5), Percentile(angles_, 0.9), Percentile(angles_, 1.0));
    }
    std::printf("   csv: %s\n", csv_path_.c_str());
    std::fflush(stdout);
}
