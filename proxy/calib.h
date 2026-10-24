// Gaze calibration: an affine map in angle space (degrees) from what the tracker reports to
// where the eyes really point, fitted from samples taken while the eyes hold a target whose
// direction is known (mode=calibrate pins the cursor in the world and the head moves around it).
#pragma once

#include "aim.h"

#include <string>
#include <vector>

namespace rezgaze {

struct Affine2 {
    // [yaw, pitch]_true = m * [yaw, pitch, 1]_measured, degrees. Identity by default.
    double m[2][3] = {{1, 0, 0}, {0, 1, 0}};
    void Apply(double yaw, double pitch, double& out_yaw, double& out_pitch) const {
        out_yaw = m[0][0] * yaw + m[0][1] * pitch + m[0][2];
        out_pitch = m[1][0] * yaw + m[1][1] * pitch + m[1][2];
    }
};

// The head-space gaze with the calibration applied to its yaw/pitch angles.
V3 ApplyCalibration(const Affine2& a, V3 gaze_head);

std::string FormatCalibration(const Affine2& a);              // "m00,m01,m02,m10,m11,m12"
bool ParseCalibration(const std::string& text, Affine2& out);

class CalibrationFit {
public:
    // One sample: what the tracker reported, and where the eyes must have pointed (degrees).
    void Add(double measured_yaw, double measured_pitch, double true_yaw, double true_pitch);
    size_t Count() const { return samples_.size(); }
    // Ranges of the true angles covered so far (degrees), for progress messages.
    void Coverage(double& yaw_range, double& pitch_range) const;
    // Least squares with outlier rejection. False, with a reason, if there are too few samples,
    // too little coverage (the head didn't move enough), or the result is implausible.
    bool Solve(Affine2& out, double& rms_deg, std::string& why) const;
    void Clear() { samples_.clear(); }

    static constexpr size_t kMinSamples = 300;
    static constexpr double kMinRangeDeg = 20;

private:
    struct S {
        double my, mp, ty, tp;
    };
    std::vector<S> samples_;
};

}  // namespace rezgaze
