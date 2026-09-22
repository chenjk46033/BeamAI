#pragma once

#include <vector>

namespace beam::correction {

// Port of BeamV0/GUIMatlab/BEAM/Correction/defineTxRxScanParams.m -- a
// struct of literal scan constants. Values are exactly the MATLAB source's
// (desiredDepth = 270/2, sampleRateHz = 45*0.65*1e6), evaluated here.
struct TxRxScanParams {
    double desiredDepth = 270.0 / 2.0;          // 135
    double sampleRateHz = 45.0 * 0.65 * 1e6;    // 29.25e6
    double voltageAmplitude = 12.0;
    std::vector<int> arrayToVSXMapping;         // [1:126, 129:254], 252 entries
};

TxRxScanParams defineTxRxScanParams();

}  // namespace beam::correction
