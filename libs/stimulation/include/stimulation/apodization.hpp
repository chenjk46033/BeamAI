#pragma once

#include <Eigen/Core>

namespace beam::stimulation {

// Port of BeamV0/GUIMatlab/BEAM/Stimulation/getApodFromAtt.m.
// vAmplitudeToMPa: volts-per-MPa calibration (the source's misspelled
// `VAmpltiudeToMPa`). att: per-element attenuation. threshAmpCorrection:
// att values below this are clamped up to it first.
//
// amplitudes = vAmplitudeToMPa ./ att_clamped; V = max(amplitudes);
// apods = amplitudes / V   (so the weakest-driven element gets apod 1).
struct ApodResult {
    double v = 0.0;
    Eigen::VectorXd apods;
};
ApodResult getApodFromAtt(double vAmplitudeToMPa, Eigen::VectorXd att, double threshAmpCorrection);

}  // namespace beam::stimulation
