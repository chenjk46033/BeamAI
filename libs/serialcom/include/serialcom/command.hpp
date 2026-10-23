#pragma once

#include <array>
#include <string>

namespace beam::serialcom {

// Seconds here; the command string carries PD/PI/BD/BI in milliseconds.
struct SonicationTiming {
    double pd = 0.0;  // pulse duration, s
    double pi = 0.0;  // pulse interval, s
    double bd = 0.0;  // burst duration, s
    double bi = 0.0;  // burst interval, s
    double startTime = 0.0;  // s
    double endTime = 0.0;    // s
};

// Builds "Stim_PR<duty>_PD<ms>_PI<ms>_BD<ms>_BI<ms>_D<s>".
std::string setSerialCommandFromStimParams(const SonicationTiming& timing, double dutyCycle);

struct CorrectionReceive {
    double ch0Max = 0.0;
    double ch1Max = 0.0;
    double rawValue = 0.0;
    double attenuation = 0.0;
    double calibrationVal = 0.0;
};

// Unparseable fields come back NaN; fewer than five throws.
CorrectionReceive parseCorrectionReceiveString(const std::string& response);

}  // namespace beam::serialcom
