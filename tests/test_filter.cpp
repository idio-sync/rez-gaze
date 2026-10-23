#include "test_main.h"

#include "../proxy/filter.h"

#include <cmath>
#include <random>

using namespace rezgaze;

namespace {

double Yaw(V3 g) { return std::atan2(g.x, -g.z) * 57.29577951308232; }
double Pitch(V3 g) { return std::atan2(g.y, -g.z) * 57.29577951308232; }

V3 Dir(double yaw_deg, double pitch_deg) {
    return Norm({std::tan(yaw_deg / 57.29577951308232), std::tan(pitch_deg / 57.29577951308232), -1});
}

}  // namespace

TEST(euro_constant_input_passes_through) {
    OneEuroGaze f(1.0, 0.05);
    V3 out;
    for (int i = 0; i < 120; ++i) out = f.Filter(Dir(10, -5), i / 60.0);
    EXPECT(std::fabs(Yaw(out) - 10) < 1e-6 && std::fabs(Pitch(out) + 5) < 1e-6);
}

TEST(euro_reduces_fixation_jitter) {
    // Fixation at (5, 2) deg with +-0.5 deg noise at 60 Hz: the output's spread must be well
    // under the input's.
    std::mt19937 rng(7);
    std::normal_distribution<double> noise(0, 0.5);
    OneEuroGaze f(1.0, 0.05);
    double in_var = 0, out_var = 0;
    int n = 0;
    for (int i = 0; i < 600; ++i) {
        double y = 5 + noise(rng), p = 2 + noise(rng);
        V3 out = f.Filter(Dir(y, p), i / 60.0);
        if (i < 60) continue;  // let it settle
        in_var += (y - 5) * (y - 5);
        out_var += (Yaw(out) - 5) * (Yaw(out) - 5);
        ++n;
    }
    EXPECT(n > 0 && out_var / n < 0.3 * in_var / n);
}

TEST(euro_follows_a_saccade_quickly) {
    // A 20 deg jump: within 3 samples (50 ms at 60 Hz) the output is within 10% of the target.
    OneEuroGaze f(1.0, 0.05);
    double t = 0;
    for (int i = 0; i < 60; ++i, t += 1 / 60.0) f.Filter(Dir(0, 0), t);
    V3 out;
    for (int i = 0; i < 3; ++i, t += 1 / 60.0) out = f.Filter(Dir(20, 0), t);
    EXPECT(Yaw(out) > 18);
}

TEST(euro_repeated_timestamp_is_safe) {
    OneEuroGaze f(1.0, 0.05);
    f.Filter(Dir(0, 0), 1.0);
    V3 out = f.Filter(Dir(10, 0), 1.0);  // dt = 0: no division by zero, output stays finite
    EXPECT(std::isfinite(out.x) && std::isfinite(out.y) && std::isfinite(out.z));
}
