#include "filter.h"

namespace rezgaze {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDeg = kPi / 180;

double Alpha(double cutoff_hz, double dt) {
    double tau = 1.0 / (2 * kPi * cutoff_hz);
    return 1.0 / (1.0 + tau / dt);
}

}  // namespace

double OneEuroGaze::Step(Axis& a, double x, double dt) {
    double dx = (x - a.x) / dt;
    a.dx += Alpha(d_cutoff_, dt) * (dx - a.dx);
    double cutoff = min_cutoff_ + beta_ * std::fabs(a.dx);
    a.x += Alpha(cutoff, dt) * (x - a.x);
    return a.x;
}

V3 OneEuroGaze::Filter(V3 gaze_head, double t) {
    V3 g = Norm(gaze_head);
    if (g.z >= 0) return out_;  // not in front of the head: keep the last output
    double yaw = std::atan2(g.x, -g.z) / kDeg, pitch = std::atan2(g.y, -g.z) / kDeg;
    if (!started_) {
        started_ = true;
        t_ = t;
        yaw_ = {yaw, 0};
        pitch_ = {pitch, 0};
        out_ = g;
        return out_;
    }
    double dt = t - t_;
    if (dt <= 0) return out_;
    t_ = t;
    double fy = Step(yaw_, yaw, dt), fp = Step(pitch_, pitch, dt);
    out_ = Norm({std::tan(fy * kDeg), std::tan(fp * kDeg), -1});
    return out_;
}

}  // namespace rezgaze
