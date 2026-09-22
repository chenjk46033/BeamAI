#pragma once

#include <Eigen/Core>

namespace beam::stimulation {

// Port of MATLAB's interp1(x, y, xq) with the default 'linear' method:
// piecewise-linear interpolation of the samples (x, y) at query point xq.
// x must be strictly ascending. Returns NaN if xq is outside [x(0), x(n-1)],
// matching MATLAB's default (no extrapolation).
double interp1(const Eigen::VectorXd& x, const Eigen::VectorXd& y, double xq);

}  // namespace beam::stimulation
