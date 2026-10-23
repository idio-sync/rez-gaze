#include "aim.h"

namespace rezgaze {

namespace {

constexpr double kDeg = 3.14159265358979323846 / 180;

// v rotated by angle a about +Y (positive turns -Z towards -X, so a negative a turns right).
V3 RotY(V3 v, double a) {
    double c = std::cos(a), s = std::sin(a);
    return {c * v.x + s * v.z, v.y, -s * v.x + c * v.z};
}

// v rotated by angle a about +X (positive turns -Z towards +Y: up).
V3 RotX(V3 v, double a) {
    double c = std::cos(a), s = std::sin(a);
    return {v.x, c * v.y - s * v.z, s * v.y + c * v.z};
}

}  // namespace

V3 ScaleGaze(V3 gaze_head, double gain_yaw, double gain_pitch) {
    V3 g = Norm(gaze_head);
    if (g.z >= 0) return g;  // not in front: leave alone
    const double cap = 80 * kDeg;
    auto scale = [&](double a, double k) {
        double s = a * k;
        return s > cap ? cap : s < -cap ? -cap : s;
    };
    double yaw = scale(std::atan2(g.x, -g.z), gain_yaw), pitch = scale(std::atan2(g.y, -g.z), gain_pitch);
    return Norm({std::tan(yaw), std::tan(pitch), -1});
}

Mat3 AimRotation(const Mat3& head, V3 gaze_head, double tilt_deg, double trim_yaw_deg, double trim_pitch_deg) {
    V3 g = RotY(RotX(Norm(gaze_head), trim_pitch_deg * kDeg), -trim_yaw_deg * kDeg);
    V3 f = Norm(Apply(head, g));
    V3 z = f * -1;
    V3 x = Norm(Cross(head.col[1], z));
    V3 y = Cross(z, x);
    // Post-multiply by Rx(tilt): columns x, y cos + z sin, -y sin + z cos.
    double t = tilt_deg * kDeg, c = std::cos(t), s = std::sin(t);
    Mat3 m;
    m.col[0] = x;
    m.col[1] = y * c + z * s;
    m.col[2] = y * -s + z * c;
    return m;
}

void GameCursorAngles(const Mat3& head, const Mat3& ctrl, bool is_oculus, double& yaw, double& pitch) {
    // R = head^T * ctrl, the controller in head space; f = R * Rx(theta) * (0, 0, -1).
    V3 d = is_oculus ? RotX({0, 0, -1}, -0.68766) : V3{0, 0, -1};
    V3 world = Apply(ctrl, d);
    V3 f{Dot(head.col[0], world), Dot(head.col[1], world), Dot(head.col[2], world)};
    yaw = std::atan2(f.x, -f.z);
    pitch = std::atan2(f.y, -f.z);
}

}  // namespace rezgaze
