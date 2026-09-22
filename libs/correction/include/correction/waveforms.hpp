#pragma once

#include <optional>

#include <Eigen/Core>

namespace beam::correction {

// Port of BeamV0/GUIMatlab/BEAM/Correction/shiftAndSumWaveforms.m.
// wvData: one waveform per row (nElements x nSamples). delaysI: per-row
// circular shift (length nElements, MATLAB circshift convention). weights:
// optional per-row weight (length nElements); defaults to all ones.
struct ShiftAndSumResult {
    Eigen::VectorXd waveformSum;  // length nSamples (MATLAB's 1 x nSamples row)
    Eigen::MatrixXd wvShifted;    // nElements x nSamples, shifted and weighted
};
ShiftAndSumResult shiftAndSumWaveforms(const Eigen::MatrixXd& wvData, const Eigen::VectorXi& delaysI,
                                        std::optional<Eigen::VectorXd> weights = std::nullopt);

}  // namespace beam::correction
