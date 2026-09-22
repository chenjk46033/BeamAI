#pragma once

#include <string>
#include <vector>

#include <Eigen/Core>

// Ported from BeamV0/GUIMatlab/BEAM/Correction/getRecieveWaveformFromSerial.m
// -- the "Run Correction" button's real hardware path (see the operator
// manual's Correction Tab section; called from `RunCorrectionButtonPushed`,
// embedded in BeamV0.mlapp itself). No callers in this project were found
// searching the plain `.m` tree; the call site only turned up by grepping
// the App Designer file's own embedded code (`GUI/BeamV0.mlapp`'s
// `matlab/document.xml`) -- so it is real and reachable, just not visible
// to a plain-text search of `Correction/`.
//
// `butter`/`filter` are MATLAB toolbox builtins, not separate `.m` files
// in the repo, so porting them is new infrastructure needed to port this
// `.m` file, not itself a port of a numbered source file.

namespace beam::correction {

struct IirCoefficients {
    Eigen::VectorXd b;
    Eigen::VectorXd a;
};

// Port of MATLAB's `butter(order, [lowNorm highNorm])` bandpass branch
// (inferred automatically by `butter` whenever its second argument is a
// two-element vector). `lowNorm`/`highNorm` are normalized to the Nyquist
// frequency (1.0 == fs/2), matching the source's own `.../(fs/2)`.
// Standard analog-prototype -> lp2bp -> bilinear-transform design
// (buttap + lp2bp + bilinear, as MATLAB's butter.m composes them), not
// reverse-engineered from the output -- cross-checked against MATLAB's
// own `butter(2, [200000 400000]/(1316800/2))` output (see
// docs/known_gaps_correction.md and matlab_verify/verify_correction.m).
// Returns `order*2 + 1` coefficients in each of b/a (a(0) == 1).
IirCoefficients butterBandpass(int order, double lowNorm, double highNorm);

// Port of MATLAB's `filter(b, a, x)` for a general IIR filter with zero
// initial conditions -- the direct-form-II-transposed recursion MATLAB's
// own `filter` uses by default.
Eigen::VectorXd filterIir(const Eigen::VectorXd& b, const Eigen::VectorXd& a, const Eigen::VectorXd& x);

// Port of the per-line parsing inside getRecieveWaveformFromSerial.m's
// polling loop: `vals = str2double(split(strtrim(line),","));
// floatData=[floatData; vals(~isnan(vals))]`. Splits on commas, parses
// each (trimmed) field as a double, and drops any field that isn't
// entirely consumed by the parse (str2double's failure mode: the whole
// field becomes NaN, then gets filtered out) -- not merely fields with a
// numeric *prefix*, matching MATLAB's strict whole-field semantics.
std::vector<double> parseCorrectionWaveformLine(const std::string& line);

struct ReceiveWaveformSplit {
    Eigen::VectorXd ch0rcv;
    Eigen::VectorXd ch1rcv;
};

// Port of getRecieveWaveformFromSerial.m's computational core, given the
// already-accumulated comma-separated float stream (the source's own
// `readline`/`NumBytesAvailable` polling loop that fills `floatData` is
// hardware/GUI glue, not ported -- same split as this project's other
// serial-command functions).
//
// N = floor(len/2); splits floatData into floatData(1:N) and
// floatData(N:end) -- note the source's own off-by-one: index N appears
// in *both* halves, a 1-sample overlap, reproduced as-is. Each half is
// filtered with butterBandpass(2, 200000/658400, 400000/658400) (fs =
// 1316800 Hz) via filterIir, then the first 99 samples are dropped
// (`ch0rcv(100:end)` in MATLAB's 1-based indexing).
//
// Disclosed deviation: the source's own `N < 2` fallback is broken --
// it assigns a typo'd `ch01rcv` instead of `ch0rcv` (so `ch0rcv` stays
// the scalar `0` from its earlier initialization), and the very next
// line, `ch0rcv(threshCut:end)`, would itself error in MATLAB (index
// exceeds a scalar's bounds). Since that path can't succeed in the
// source either, this throws std::invalid_argument instead of
// replicating a MATLAB crash.
ReceiveWaveformSplit splitAndFilterReceiveWaveform(const std::vector<double>& floatData);

}  // namespace beam::correction
