#pragma once

#include <array>
#include <string>

namespace beam::serialcom {

// The sonication timing fields setSerialCommandFromStimParams.m reads out
// of stimParams(1). Durations in seconds (the source multiplies PD/PI/BD/BI
// by 1000 to send milliseconds).
struct SonicationTiming {
    double pd = 0.0;  // pulse duration, s
    double pi = 0.0;  // pulse interval, s
    double bd = 0.0;  // burst duration, s
    double bi = 0.0;  // burst interval, s
    double startTime = 0.0;  // s
    double endTime = 0.0;    // s
};

// Port of BeamV0/GUIMatlab/BEAM/SerialCom/setSerialCommandFromStimParams.m.
// Builds the "Stim_PR<duty>_PD<ms>_PI<ms>_BD<ms>_BI<ms>_D<s>" command
// string for the STM32 firmware.
//
// The MATLAB source formats each number with num2str (no format arg).
// This uses "%.5g", which matches num2str's default 5-significant-digit
// behaviour for the value ranges here (duty cycle 0..1, timings in the
// 1..1000 ms range); it is not a byte-exact reimplementation of num2str
// for all inputs.
std::string setSerialCommandFromStimParams(const SonicationTiming& timing, double dutyCycle);

// Port of the pure parsing core of
// BeamV0/GUIMatlab/BEAM/SerialCom/parseCorrectionReceiveString.m -- the
// `split(response,',')` + `str2double` of the 5 comma-separated fields.
// The serial-read / app-UI parts of that function are not ported.
// Unparseable fields come back as NaN (matching str2double); fewer than 5
// fields throws std::runtime_error.
struct CorrectionReceive {
    double ch0Max = 0.0;
    double ch1Max = 0.0;
    double rawValue = 0.0;
    double attenuation = 0.0;
    double calibrationVal = 0.0;
};
CorrectionReceive parseCorrectionReceiveString(const std::string& response);

}  // namespace beam::serialcom
