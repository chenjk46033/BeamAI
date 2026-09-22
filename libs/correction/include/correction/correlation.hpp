#pragma once

#include <optional>

#include <Eigen/Core>

// Ported from BeamV0/GUIMatlab/BEAM/Correction/: xcorrS1ToS2.m,
// corrSpeedUp.m, computeMaxCorrelationDelays.m -- cross-correlation-based
// delay estimation. These operate on plain signal vectors/matrices; their
// real callers in BeamV0 are either dead code in the current app or live in
// localizeArrays/ (deferred, needs a nonlinear least-squares solver).
// Ported ahead of those consumers anyway, since they're self-contained,
// reusable DSP.

namespace beam::correction {

struct XcorrResult {
    Eigen::VectorXd c;     // cross-correlation values, length 2*n-1
    Eigen::VectorXi lags;  // corresponding lags, -(n-1)..(n-1)
};

// Port of MATLAB's built-in xcorr(x,y) (default 'none' scaling): zero-pads
// the shorter of x/y to the other's length, then the direct O(n^2)
// definition. Not FFT-based -- fine at the signal lengths BeamV0's real
// callers use; revisit if that changes.
XcorrResult xcorr(const Eigen::VectorXd& x, const Eigen::VectorXd& y);

// Port of MATLAB's corrcoef(a,b) followed by (1,2) -- the Pearson
// correlation coefficient returned directly as a scalar (every BeamV0 call
// site immediately extracts (1,2)). NaN-propagating, matching MATLAB.
double corrCoefficient(const Eigen::VectorXd& a, const Eigen::VectorXd& b);

// Port of MATLAB's nanmean(v). Returns NaN if every entry is NaN.
double nanmean(const Eigen::VectorXd& v);

// Port of Correction/xcorrS1ToS2.m. The MATLAB source's third output (the
// modified s1) is dropped: its one real caller
// (localizeArrays/getLocalizeArraysSystemOfEquations.m) only captures
// [d12, cc].
struct XcorrS1ToS2Result {
    int d12 = 0;
    double cc = 0.0;
};
XcorrS1ToS2Result xcorrS1ToS2(Eigen::VectorXd s1, Eigen::VectorXd s2, const Eigen::Vector2d& bounds);

// Port of Correction/corrSpeedUp.m. The `varargin` custom-bounds override
// is dropped (no BeamV0 call site uses it), so the source's default bounds
// lb=0, ub=-150 are hardcoded. The modified ssignal/nssignal outputs are
// dropped too -- not captured anywhere.
struct CorrSpeedUpResult {
    int speedupsa = 0;
    double cc = 0.0;
};
CorrSpeedUpResult corrSpeedUp(Eigen::VectorXd ssignal, Eigen::VectorXd nssignal);

// Port of Correction/computeMaxCorrelationDelays.m. maxChannel: 1-based
// channel (row) index, or std::nullopt to auto-detect (MATLAB's
// Defaults{1}=[] sentinel) as the row of the largest absolute sample,
// scanned in column-major order so ties resolve like MATLAB's `find`.
// ccFlag defaults to true (MATLAB's Defaults{2}=1).
struct MaxCorrelationDelaysResult {
    Eigen::VectorXi delaysI;  // length nElements; the maxChannel row stays 0
    int maxChannel = 0;       // 1-based, echoed back even when passed in
    Eigen::VectorXd cc;       // length nElements; maxChannel row stays 0
};
MaxCorrelationDelaysResult computeMaxCorrelationDelays(const Eigen::MatrixXd& wvData,
                                                        std::optional<int> maxChannel = std::nullopt,
                                                        bool ccFlag = true);

}  // namespace beam::correction
