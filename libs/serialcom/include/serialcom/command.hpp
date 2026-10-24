#pragma once

#include <array>
#include <string>
#include <vector>

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

// Trimmed and single-spaced, so a reply can be compared with what was
// expected without tripping over spacing.
std::string normaliseEcho(const std::string& text);

// Fields of an echo that differ by more than `relativeTolerance` (with a
// one-unit floor), named for reporting. Empty when the two agree.
std::vector<std::string> stimEchoDifferences(const std::string& expected, const std::string& actual,
                                             double relativeTolerance = 0.001);

// What the device echoes for a Stim command: duty as a percentage, timings
// as 10-per-ms timer ticks, and PI/BI as off-times rather than intervals.
// Lets the reply be compared against what was sent.
std::string expectedStimEcho(const SonicationTiming& timing, double dutyCycle);

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
