#include "calib.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace rezgaze {

namespace {

constexpr double kDeg = 3.14159265358979323846 / 180;

// Solves the 3x3 system a * x = b (Gaussian elimination with partial pivoting).
bool Solve3(double a[3][3], double b[3], double x[3]) {
    for (int c = 0; c < 3; ++c) {
        int p = c;
        for (int r = c + 1; r < 3; ++r)
            if (std::fabs(a[r][c]) > std::fabs(a[p][c])) p = r;
        if (std::fabs(a[p][c]) < 1e-12) return false;
        std::swap(a[c], a[p]);
        std::swap(b[c], b[p]);
        for (int r = c + 1; r < 3; ++r) {
            double f = a[r][c] / a[c][c];
            for (int k = c; k < 3; ++k) a[r][k] -= f * a[c][k];
            b[r] -= f * b[c];
        }
    }
    for (int r = 2; r >= 0; --r) {
        double s = b[r];
        for (int k = r + 1; k < 3; ++k) s -= a[r][k] * x[k];
        x[r] = s / a[r][r];
    }
    return true;
}

}  // namespace

V3 ApplyCalibration(const Affine2& a, V3 gaze_head) {
    V3 g = Norm(gaze_head);
    if (g.z >= 0) return g;
    double yaw = std::atan2(g.x, -g.z) / kDeg, pitch = std::atan2(g.y, -g.z) / kDeg, cy, cp;
    a.Apply(yaw, pitch, cy, cp);
    cy = std::clamp(cy, -80.0, 80.0);
    cp = std::clamp(cp, -80.0, 80.0);
    return Norm({std::tan(cy * kDeg), std::tan(cp * kDeg), -1});
}

std::string FormatCalibration(const Affine2& a) {
    char buf[160];
    std::snprintf(buf, sizeof buf, "%.6f,%.6f,%.6f,%.6f,%.6f,%.6f", a.m[0][0], a.m[0][1], a.m[0][2], a.m[1][0],
                  a.m[1][1], a.m[1][2]);
    return buf;
}

bool ParseCalibration(const std::string& text, Affine2& out) {
    Affine2 a;
    if (std::sscanf(text.c_str(), "%lf,%lf,%lf,%lf,%lf,%lf", &a.m[0][0], &a.m[0][1], &a.m[0][2], &a.m[1][0],
                    &a.m[1][1], &a.m[1][2]) != 6)
        return false;
    out = a;
    return true;
}

void CalibrationFit::Add(double measured_yaw, double measured_pitch, double true_yaw, double true_pitch) {
    samples_.push_back({measured_yaw, measured_pitch, true_yaw, true_pitch});
}

void CalibrationFit::Coverage(double& yaw_range, double& pitch_range) const {
    if (samples_.empty()) {
        yaw_range = pitch_range = 0;
        return;
    }
    double y0 = 1e9, y1 = -1e9, p0 = 1e9, p1 = -1e9;
    for (const S& s : samples_) {
        y0 = std::min(y0, s.ty), y1 = std::max(y1, s.ty);
        p0 = std::min(p0, s.tp), p1 = std::max(p1, s.tp);
    }
    yaw_range = y1 - y0;
    pitch_range = p1 - p0;
}

bool CalibrationFit::Solve(Affine2& out, double& rms_deg, std::string& why) const {
    char buf[200];
    if (samples_.size() < kMinSamples) {
        std::snprintf(buf, sizeof buf, "too few samples (%zu of %zu): keep going", samples_.size(), kMinSamples);
        why = buf;
        return false;
    }
    double yr, pr;
    Coverage(yr, pr);
    if (yr < kMinRangeDeg || pr < kMinRangeDeg) {
        std::snprintf(buf, sizeof buf,
                      "the eyes covered only %.0f deg across and %.0f deg up/down (need %.0f): move your head "
                      "further while keeping your eyes on the cursor",
                      yr, pr, kMinRangeDeg);
        why = buf;
        return false;
    }

    // Fit, then drop samples far off the fit (blinks, glances away) and refit.
    std::vector<bool> keep(samples_.size(), true);
    Affine2 a;
    for (int pass = 0; pass < 3; ++pass) {
        for (int out_row = 0; out_row < 2; ++out_row) {
            double ata[3][3] = {}, atb[3] = {};
            for (size_t i = 0; i < samples_.size(); ++i) {
                if (!keep[i]) continue;
                const S& s = samples_[i];
                double x[3] = {s.my, s.mp, 1}, t = out_row == 0 ? s.ty : s.tp;
                for (int r = 0; r < 3; ++r) {
                    atb[r] += x[r] * t;
                    for (int c = 0; c < 3; ++c) ata[r][c] += x[r] * x[c];
                }
            }
            double sol[3];
            if (!Solve3(ata, atb, sol)) {
                why = "the samples don't determine a fit (no spread in the tracker's readings)";
                return false;
            }
            for (int c = 0; c < 3; ++c) a.m[out_row][c] = sol[c];
        }
        std::vector<double> res(samples_.size());
        std::vector<double> kept;
        for (size_t i = 0; i < samples_.size(); ++i) {
            double cy, cp;
            a.Apply(samples_[i].my, samples_[i].mp, cy, cp);
            res[i] = std::hypot(cy - samples_[i].ty, cp - samples_[i].tp);
            if (keep[i]) kept.push_back(res[i]);
        }
        std::nth_element(kept.begin(), kept.begin() + kept.size() / 2, kept.end());
        double limit = std::max(3 * kept[kept.size() / 2], 1.0);
        for (size_t i = 0; i < samples_.size(); ++i) keep[i] = res[i] <= limit;
    }

    double sum = 0;
    size_t n = 0;
    for (size_t i = 0; i < samples_.size(); ++i) {
        if (!keep[i]) continue;
        double cy, cp;
        a.Apply(samples_[i].my, samples_[i].mp, cy, cp);
        sum += (cy - samples_[i].ty) * (cy - samples_[i].ty) + (cp - samples_[i].tp) * (cp - samples_[i].tp);
        ++n;
    }
    if (n < kMinSamples / 2) {
        why = "most samples disagreed with each other (eyes not on the cursor?)";
        return false;
    }
    rms_deg = std::sqrt(sum / n);
    if (a.m[0][0] < 0.5 || a.m[0][0] > 2 || a.m[1][1] < 0.5 || a.m[1][1] > 2) {
        std::snprintf(buf, sizeof buf, "implausible gains %.2f / %.2f (eyes not on the cursor?)", a.m[0][0],
                      a.m[1][1]);
        why = buf;
        return false;
    }
    out = a;
    return true;
}

}  // namespace rezgaze
