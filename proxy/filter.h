// One Euro filter (Casiez et al. 2012) on the gaze's yaw and pitch angles (degrees): heavy
// smoothing while the eyes rest, little while they move, so fixations steady without slowing
// saccades (which REZ.exe's lock-on rewards: it locks faster the faster the cursor sweeps).
#pragma once

#include "aim.h"

namespace rezgaze {

class OneEuroGaze {
public:
    // min_cutoff in Hz (lower = steadier at rest), beta per deg/s (higher = less lag in motion).
    OneEuroGaze(double min_cutoff, double beta, double d_cutoff = 5.0)
        : min_cutoff_(min_cutoff), beta_(beta), d_cutoff_(d_cutoff) {}

    void SetParams(double min_cutoff, double beta) {
        min_cutoff_ = min_cutoff;
        beta_ = beta;
    }

    // Head-space gaze in, filtered head-space gaze out; t in seconds. A repeated or earlier t
    // returns the previous output.
    V3 Filter(V3 gaze_head, double t);

private:
    struct Axis {
        double x = 0, dx = 0;
    };
    double Step(Axis& a, double x, double dt);

    double min_cutoff_, beta_, d_cutoff_;
    bool started_ = false;
    double t_ = 0;
    Axis yaw_, pitch_;
    V3 out_{0, 0, -1};
};

}  // namespace rezgaze
