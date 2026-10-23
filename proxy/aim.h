// Aim math: the controller rotation that makes REZ.exe's cursor land on the gaze.
// Conventions (OpenVR's): +X right, +Y up, -Z forward; matrices are columns = axes in world.
#pragma once

#include <cmath>

namespace rezgaze {

struct V3 {
    double x = 0, y = 0, z = 0;
};
inline V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V3 operator*(V3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }
inline double Dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 Cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline V3 Norm(V3 a) {
    double l = std::sqrt(Dot(a, a));
    return l > 1e-12 ? a * (1 / l) : V3{0, 0, -1};
}

struct Mat3 {
    V3 col[3];
};

inline V3 Apply(const Mat3& m, V3 v) { return m.col[0] * v.x + m.col[1] * v.y + m.col[2] * v.z; }

// The head-space gaze with its yaw and pitch angles (atan2(x,-z), atan2(y,-z)) multiplied by the
// gains, each capped at 80 degrees. For when the cursor doesn't reach as far as the eyes look.
V3 ScaleGaze(V3 gaze_head, double gain_yaw, double gain_pitch);

// The controller rotation (world) whose -Z points where the eyes look. gaze_head is the gaze in
// head space; trim_yaw_deg (positive = right) and trim_pitch_deg (positive = up) shift it in
// head space; roll follows the head's up. tilt_deg rotates the result about its own X, to undo
// a tilt the game applies (+39.4 for REZ.exe's "oculus" controllers).
Mat3 AimRotation(const Mat3& head, V3 gaze_head, double tilt_deg, double trim_yaw_deg, double trim_pitch_deg);

// REZ.exe's cursor angles in "motion controller only" mode (docs/aim-re.md), in radians:
// the controller relative to the head, with the -0.68766 rad tilt about X if is_oculus, then
// yaw = atan2(f.x, -f.z), pitch = atan2(f.y, -f.z) of its -Z. For tests.
void GameCursorAngles(const Mat3& head, const Mat3& ctrl, bool is_oculus, double& yaw, double& pitch);

}  // namespace rezgaze
