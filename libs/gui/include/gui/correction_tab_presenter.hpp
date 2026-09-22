#pragma once

#include <Eigen/Core>

// GUI phase, Correction-tab slice: the data-layer logic behind
// BeamV0/GUIMatlab/BEAM/GUI/CorrectionTab/*.m, decoupled from the Qt
// drawing calls (libs/gui_qt renders these). No Qt dependency.
//
// Not ported (dead in BeamV0 -- no callers anywhere in the tree):
//   setImagescMask.m         -- mask-overlay compositing, never called
//   formatAttenuationPlotAxes.m -- colorbar/caxis/axtoolbar chrome, never called
// updateAttenuationPlots.m is live (from initializeCorrectionValues.m).
// Its own plots are RF-waveform line plots, not a heatmap (unlike Diadem's
// same-named file). initializeCorrectionValues.m's own literal defaults are
// ported below as correctionInitialState().

namespace beam::gui {

// The two-bar "Current vs Average" transmission comparison from
// GUI/CorrectionTab/setAvgTransmissionDataBars.m.
//
// Disclosed source quirk: the bar *color* and the *pass flag* come from
// different quantities. plotAttBars colors the "Current" bar red when
// `att < 0.2 - 0.1` (averageValue - averageStd) and returns a matching
// flag -- but setAvgTransmissionDataBars.m then immediately overwrites that
// flag with `transmissionPeak2Peak > couplingThreshold`. Both are kept:
// `currentBarIsRed` drives the drawing, `pass` is the real
// checkAvgTransmissionPass.
struct AvgTransmissionBars {
    double currentValue = 0.0;    // app.sys.RTT(1).att
    double averageValue = 0.2;    // hardcoded in the source
    double currentStd = 0.0;      // hardcoded
    double averageStd = 0.1;      // hardcoded
    bool currentBarIsRed = false;  // att < averageValue - averageStd
    bool pass = false;             // transmissionPeak2Peak > couplingThreshold
};
AvgTransmissionBars computeAvgTransmissionBars(double att, double transmissionPeak2Peak,
                                                double couplingThreshold);

// Port of updateAttenuationPlots.m's `ULBound`: the symmetric y-limit for
// the two RF-waveform plots, `max(max|x0|, max|x1|) + 10`.
double computeRfPlotYLimit(const Eigen::VectorXd& x0Filtered, const Eigen::VectorXd& x1Filtered);

// Port of initializeCorrectionValues.m's literal startup defaults --
// app.sys.arrayData.arrayTotal.medianAttenuation = [1,1] and
// app.sys.RTT(1)'s ch0Max/ch1Max/correctionVal/att/calibrationVal, all 1 --
// the state before any real correction has ever run. The source calls
// updateAttenuationPlots(app) right after setting these; that plot's own
// y-limit is computeRfPlotYLimit above, unaffected by these scalars.
struct CorrectionInitialState {
    double medianAttenuationCh0 = 1.0;
    double medianAttenuationCh1 = 1.0;
    double ch0Max = 1.0;
    double ch1Max = 1.0;
    double correctionVal = 1.0;
    double att = 1.0;
    double calibrationVal = 1.0;
};
CorrectionInitialState correctionInitialState();

}  // namespace beam::gui
