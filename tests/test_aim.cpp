#include "test_main.h"

#include "../proxy/aim.h"

#include <cmath>
#include <random>

using namespace rezgaze;

namespace {

constexpr double kDeg = 3.14159265358979323846 / 180;

// Rotation by angle a about unit axis u (Rodrigues), as columns.
Mat3 AxisAngle(V3 u, double a) {
    double c = std::cos(a), s = std::sin(a), t = 1 - c;
    Mat3 m;
    m.col[0] = {t * u.x * u.x + c, t * u.x * u.y + s * u.z, t * u.x * u.z - s * u.y};
    m.col[1] = {t * u.x * u.y - s * u.z, t * u.y * u.y + c, t * u.y * u.z + s * u.x};
    m.col[2] = {t * u.x * u.z + s * u.y, t * u.y * u.z - s * u.x, t * u.z * u.z + c};
    return m;
}

struct Case {
    Mat3 head;
    V3 gaze;
};

// Random head orientations, and gaze within 40 degrees of straight ahead.
std::vector<Case> Cases() {
    std::mt19937 rng(12345);
    std::uniform_real_distribution<double> u(-1, 1);
    std::vector<Case> out;
    for (int i = 0; i < 1000; ++i) {
        V3 axis = Norm({u(rng), u(rng), u(rng)});
        Mat3 head = AxisAngle(axis, u(rng) * 3.1);
        double yaw = u(rng) * 40 * kDeg / std::sqrt(2.0), pitch = u(rng) * 40 * kDeg / std::sqrt(2.0);
        V3 g = Norm({std::tan(yaw), std::tan(pitch), -1});
        out.push_back({head, g});
    }
    return out;
}

double Det(const Mat3& m) { return Dot(m.col[0], Cross(m.col[1], m.col[2])); }

}  // namespace

TEST(aim_matches_game_not_oculus) {
    int bad = 0;
    for (const Case& c : Cases()) {
        Mat3 ctrl = AimRotation(c.head, c.gaze, 0, 0, 0);
        double yaw, pitch;
        GameCursorAngles(c.head, ctrl, false, yaw, pitch);
        if (std::fabs(yaw - std::atan2(c.gaze.x, -c.gaze.z)) > 1e-6) ++bad;
        if (std::fabs(pitch - std::atan2(c.gaze.y, -c.gaze.z)) > 1e-6) ++bad;
    }
    EXPECT(bad == 0);
}

TEST(aim_matches_game_oculus) {
    int bad = 0;
    for (const Case& c : Cases()) {
        Mat3 ctrl = AimRotation(c.head, c.gaze, 39.4, 0, 0);
        double yaw, pitch;
        GameCursorAngles(c.head, ctrl, true, yaw, pitch);
        if (std::fabs(yaw - std::atan2(c.gaze.x, -c.gaze.z)) > 1e-3) ++bad;
        if (std::fabs(pitch - std::atan2(c.gaze.y, -c.gaze.z)) > 1e-3) ++bad;
    }
    EXPECT(bad == 0);
}

TEST(aim_oculus_without_tilt_is_off) {
    // Guards the test above: forgetting the tilt must show up as a large error.
    Case c = Cases()[0];
    Mat3 ctrl = AimRotation(c.head, c.gaze, 0, 0, 0);
    double yaw, pitch;
    GameCursorAngles(c.head, ctrl, true, yaw, pitch);
    EXPECT(std::fabs(pitch - std::atan2(c.gaze.y, -c.gaze.z)) > 0.3);
}

TEST(aim_trim) {
    Mat3 id;
    id.col[0] = {1, 0, 0};
    id.col[1] = {0, 1, 0};
    id.col[2] = {0, 0, 1};
    double yaw, pitch;
    GameCursorAngles(id, AimRotation(id, {0, 0, -1}, 0, 2, 0), false, yaw, pitch);
    EXPECT(std::fabs(yaw - 2 * kDeg) < 1e-6 && std::fabs(pitch) < 1e-6);
    GameCursorAngles(id, AimRotation(id, {0, 0, -1}, 0, 0, 3), false, yaw, pitch);
    EXPECT(std::fabs(pitch - 3 * kDeg) < 1e-6 && std::fabs(yaw) < 1e-6);
}

TEST(aim_orthonormal) {
    int bad = 0;
    for (const Case& c : Cases()) {
        Mat3 m = AimRotation(c.head, c.gaze, 39.4, 1, -1);
        for (int i = 0; i < 3; ++i) {
            if (std::fabs(Dot(m.col[i], m.col[i]) - 1) > 1e-9) ++bad;
            if (std::fabs(Dot(m.col[i], m.col[(i + 1) % 3])) > 1e-9) ++bad;
        }
        if (std::fabs(Det(m) - 1) > 1e-9) ++bad;
    }
    EXPECT(bad == 0);
}
