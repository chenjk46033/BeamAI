#pragma once

#include <vector>

#include <Eigen/Core>

namespace beam::correction {

// Port of MATLAB's findpeaks(v) with no options: indices of strict local
// maxima (v(i-1) < v(i) > v(i+1)), in ascending order, 0-based. Endpoints
// are never peaks, and flat tops are not peaks -- matching MATLAB.
std::vector<Eigen::Index> findPeaks(const Eigen::VectorXd& v);

// Port of BeamV0/BEAMANALYSIS/analysisUtil/findPeakNegativeVoltage.m
// (a Correction/ dependency, pulled in here the way the Arrays phase pulled
// in the Util helpers it needed). The `varargin` debug-plot argument is
// dropped.
//
// nCycles < 2: returns -min(v).
// Otherwise: mean-subtract, take abs, find peaks, and return the median of
// the nCycles largest. If there are fewer than nCycles peaks (or none), it
// falls back to -min(abs(v - mean(v))) -- faithfully odd, but that's what
// the source's `pnv = -min(v)` does after v has been reassigned to
// abs(v - mean(v)).
double findPeakNegativeVoltage(const Eigen::VectorXd& v, int nCycles);

}  // namespace beam::correction
