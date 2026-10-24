#include "test_main.h"

#include "../proxy/calib.h"

#include <cmath>
#include <random>

using namespace rezgaze;

namespace {

// A tracker that reads off by an affine map: measured = M * required + offset (degrees).
struct FakeTracker {
    double gy = 0.85, sy = 0.05, oy = 2.0;    // yaw: gain, pitch->yaw skew, offset
    double gp = 0.9, sp = -0.03, op = -1.5;   // pitch
    void Measure(double ry, double rp, double& my, double& mp) const {
        my = gy * ry + sy * rp + oy;
        mp = sp * ry + gp * rp + op;
    }
};

// Required angles sweeping a circle and a cross within +-22 deg, as a head moving around a
// pinned cursor would give.
void Sweep(CalibrationFit& fit, const FakeTracker& t, double noise_deg, double outlier_frac, unsigned seed) {
    std::mt19937 rng(seed);
    std::normal_distribution<double> noise(0, noise_deg);
    std::uniform_real_distribution<double> u(0, 1), wild(-40, 40);
    for (int i = 0; i < 1500; ++i) {
        double a = i / 1500.0 * 6 * 3.14159265358979;
        double ry = 22 * std::cos(a) * (i % 3 == 0 ? 0.5 : 1), rp = 18 * std::sin(a);
        double my, mp;
        t.Measure(ry, rp, my, mp);
        my += noise(rng);
        mp += noise(rng);
        if (u(rng) < outlier_frac) my = wild(rng), mp = wild(rng);  // blinks, glances away
        fit.Add(my, mp, ry, rp);
    }
}

}  // namespace

TEST(calib_identity_changes_nothing) {
    Affine2 id;
    V3 g = Norm({0.2, -0.1, -1});
    V3 c = ApplyCalibration(id, g);
    EXPECT(std::fabs(c.x - g.x) < 1e-12 && std::fabs(c.y - g.y) < 1e-12 && std::fabs(c.z - g.z) < 1e-12);
}

TEST(calib_recovers_offset_gain_and_skew) {
    FakeTracker t;
    CalibrationFit fit;
    Sweep(fit, t, 0.3, 0, 1);
    Affine2 a;
    double rms;
    std::string why;
    EXPECT(fit.Solve(a, rms, why));
    EXPECT(rms < 0.5);
    // Applying the fit to a measurement gives back the required angles.
    double my, mp, cy, cp;
    t.Measure(10, -8, my, mp);
    a.Apply(my, mp, cy, cp);
    EXPECT(std::fabs(cy - 10) < 0.2 && std::fabs(cp + 8) < 0.2);
}

TEST(calib_ignores_outliers) {
    FakeTracker t;
    CalibrationFit fit;
    Sweep(fit, t, 0.3, 0.05, 2);
    Affine2 a;
    double rms;
    std::string why;
    EXPECT(fit.Solve(a, rms, why));
    double my, mp, cy, cp;
    t.Measure(-15, 12, my, mp);
    a.Apply(my, mp, cy, cp);
    EXPECT(std::fabs(cy + 15) < 0.3 && std::fabs(cp - 12) < 0.3);
}

TEST(calib_refuses_without_coverage) {
    // The head barely moved: the eyes stayed within +-3 deg, so gain can't be measured.
    FakeTracker t;
    CalibrationFit fit;
    for (int i = 0; i < 1500; ++i) {
        double ry = 3 * std::cos(i * 0.01), rp = 3 * std::sin(i * 0.01), my, mp;
        t.Measure(ry, rp, my, mp);
        fit.Add(my, mp, ry, rp);
    }
    Affine2 a;
    double rms;
    std::string why;
    EXPECT(!fit.Solve(a, rms, why));
    EXPECT(why.find("move") != std::string::npos);
}

TEST(calib_refuses_too_few_samples) {
    CalibrationFit fit;
    for (int i = 0; i < 50; ++i) fit.Add(i * 0.5, -i * 0.3, i * 0.5, -i * 0.3);
    Affine2 a;
    double rms;
    std::string why;
    EXPECT(!fit.Solve(a, rms, why));
}

TEST(calib_text_roundtrip) {
    Affine2 a;
    a.m[0][0] = 1.17;
    a.m[0][1] = -0.05;
    a.m[0][2] = -2.3;
    a.m[1][0] = 0.02;
    a.m[1][1] = 1.1;
    a.m[1][2] = 1.6;
    Affine2 b;
    EXPECT(ParseCalibration(FormatCalibration(a), b));
    for (int r = 0; r < 2; ++r)
        for (int c = 0; c < 3; ++c) EXPECT(std::fabs(a.m[r][c] - b.m[r][c]) < 1e-6);
    EXPECT(!ParseCalibration("garbage", b));
}
