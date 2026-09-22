#pragma once

#include <Eigen/Core>

namespace beam::stimulation {

// Port of BeamV0/GUIMatlab/BEAM/Stimulation/pressureToDutyCycleGivenTransmission.m.
// P: requested pressure amplitude. T: measured through-transmit
// transmission. calDuty / calPressure: the calibration curve (duty-cycle %
// vs. pressure), calPressure strictly ascending.
//
// Maps (P, T) through the source's nonlinear T->P adjustment, looks the
// result up on the calibration curve via interp1, converts % to fraction,
// and clamps to [0.4, 0.75]. If the adjusted pressure falls outside the
// calibration range interp1 yields NaN and the result clamps to 0.4 --
// matching MATLAB's max(NaN, 0.4).
double pressureToDutyCycleGivenTransmission(double p, double t, const Eigen::VectorXd& calDuty,
                                             const Eigen::VectorXd& calPressure);

}  // namespace beam::stimulation
