#pragma once

#include <string>
#include <vector>

#include <Eigen/Core>

#include "gui/top_targets.hpp"  // for AccFlag

// GUI phase, SonicationTab slice: the pure data-transform logic behind
// BeamV0/GUIMatlab/BEAM/GUI/SonicationTab/*.m, decoupled from the
// `app`/`uitable` reads and writes. No Qt dependency.
//
// NOT ported this slice, disclosed rather than attempted:
// `getTopTargetsFromTreatmentProtocolTable.m` -- a large (~150-line),
// intricate target-ranking/carry-forward algorithm over historical
// mood/pain responses. Real clinical-protocol logic, not GUI glue, but
// substantial enough to deserve its own dedicated slice rather than being
// rushed alongside the smaller functions here.

namespace beam::gui {

// Port of getResponseFromTreatmentProtocolData.m's response computation
// (the `app.treatmentProtocolTable.Data.Response*(i) = ...` UI write-back
// is not carried -- that's the caller's table to update). NaN inputs
// (MATLAB: `~isnumeric(x) || isnan(x)`, the `isnumeric` half moot for a
// C++ double) map to 0; both are then clamped to [-2, 2].
struct MoodPainResponse {
    int mood = 0;
    int pain = 0;
    int total = 0;  // pain + mood
};
MoodPainResponse getResponseFromTreatmentProtocolData(double responseMoodRaw, double responsePainRaw);

// Port of getNewProtocolName.m: finds a "Protocol <N>" name that doesn't
// collide with `existingProtocolNames[0 .. size()-2]` (the source excludes
// the last entry -- presumably the not-yet-named new table itself).
// N = existingProtocolNames.size(). Faithful-port quirk: on a collision,
// the first retry appends "_01"; every retry after that overwrites just
// the *last character* of the name with the next digit (MATLAB
// `newName(end) = num2str(j)`) -- reproduced exactly, not "fixed" into a
// suffix counter.
std::string getNewProtocolName(const std::vector<std::string>& existingProtocolNames);

// Port of setColorMapRGB.m: 13 fixed colors followed by scaled greens.
//
// Faithful-port bug, preserved exactly: the source means to build 100
// colors (`N = 100`) but its fill loop runs `for i = (NN+1):nLeft` where
// `nLeft = N - NN = 87` -- using `nLeft` as the loop bound instead of `N`.
// The result has 87 rows, not 100, and only the first 74 of the 87
// `linspace(0.1, 0.9, nLeft)` scale values are ever used, so the scaled
// greens only reach ~0.78 saturation, never the intended 0.9.
Eigen::MatrixX3d colorMapRgb();

// Port of getCurrentShownSonication.m: the 1-based index of the last row
// (scanning from the end) whose `show` flag is set; 1 if none are (or the
// table is empty) -- matching the source's `i == 0 -> rowi = 1` fallback.
int getCurrentShownSonication(const std::vector<bool>& show);

// Port of sortSonicationTable.m's ordering (the `app.stimParamTable.Data`
// reorder itself is the caller's table to apply). Returns the 0-based
// permutation that stable-sorts `order` ascending (MATLAB `sort` is stable
// for ties, matching `std::stable_sort`).
std::vector<int> sortSonicationTableOrder(const std::vector<double>& order);

// Port of updateSonicationPlots.m's first plot (`axPulseWaveformPlot`):
// a step function that is `amplitude` for x <= pulseDuration within one
// pulse interval, 0 after, sampled at the source's hardcoded 650 kHz.
struct PulseWaveformPlot {
    Eigen::VectorXd x;
    Eigen::VectorXd y;
};
PulseWaveformPlot computePulseWaveformPlot(double pulseDuration, double pulseInterval,
                                            double amplitude);

// Port of updateSonicationPlots.m's second plot (`axBurstWaveformPlot`):
// the repeating pulse-train across one burst, `amplitude` for x <=
// burstDuration and 0 after, sampled at a fixed 100000 points per burst
// (`fs = 100000/BI`, matching the source). Faithful-port quirk,
// preserved: the two per-sample conditions aren't mutually exclusive
// (both can independently set a sample on), and the PI-boundary-crossing
// condition uses strict `>`, matching the source exactly rather than a
// cleaner on/off derivation.
PulseWaveformPlot computeBurstWaveformPlot(double burstDuration, double burstInterval, double pulseDuration,
                                            double pulseInterval, double amplitude);

// Port of setTreatmentProtocolTableData.m's per-row coloring rule (the
// `uistyle`/`addStyle` calls themselves are GUI, not ported): negative
// response -> yellow; zero -> white; positive -> green, intensity scaled
// by `min(currResponse, 4) / 4` (so a response of 4 or more is the same
// full-intensity green). RGB components in [0, 1], matching MATLAB's
// uistyle BackgroundColor convention.
struct RowColor {
    double r = 1.0, g = 1.0, b = 1.0;
};
RowColor computeTreatmentRowColor(double currResponse);

// Port of setTreatmentProtocolTableData.m's ACCFlag selection.
//
// Faithful-port quirk, preserved: the source builds a *pair* of region
// names per branch (`{'SCC','aMCC'}` or `{'aMCC','SCC'}`) but only ever
// reads the first element at its one real call site -- the second is dead.
// And `'PainACC'` passes the literal string `'ACC'` as the ACCFlag, which
// matches neither `getTopTargetsFromTreatmentProtocolTable.m`'s `'SCC'`
// nor `'aMCC'` branch, so it silently falls into the interleaved "other"
// ranking -- reproduced as AccFlag::kOther, not a dedicated ACC case.
AccFlag accFlagForProtocolName(const std::string& protocolName);

// Port of getNewProtocolName.m: `"Protocol " + N` (N = the number of
// existing protocols), then de-duplicated against every existing name
// except the *last* one (the source's own `1:end-1` range -- preserved,
// not corrected).
//
// Faithful-port quirk, preserved: the outer `j = 1:5` loop re-runs the
// *entire* inner comparison every pass rather than stopping once no
// collision is found, so a name earlier in the list can still trigger a
// second rename on a later pass even though the current name already
// changed once. On the first collision anywhere (`j == 1`) the source
// appends the literal suffix `"_01"`; on every later pass (`j` = 2..5)
// it doesn't append `_0j` as the pattern might suggest --
// `newName(end) = num2str(j)` *overwrites* the name's last character
// with the single digit `j` instead.
std::string newProtocolName(const std::vector<std::string>& existingNames);

// Port of SonicationTab/ExampleTargets/setExampleTargetImages.m's target
// -> help-text mapping only (the `TargetingTextArea.Value` string). Not
// ported: the `imread`/`imshow` calls loading `SCC1Sag.png` etc -- those
// reference images aren't bundled with this repo. Unknown flags fall
// through to the source's own default: the initial `text` value.
std::string exampleTargetHelpText(const std::string& exampleFlag);

}  // namespace beam::gui
