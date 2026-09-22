#include "safety/intensity.hpp"

#include <cmath>

namespace beam::safety {

namespace {
constexpr double kSpeedOfSound = 1546.0;  // m/s, both source functions
}  // namespace

double isppa(double amplitudeMPa, double density) {
    const double pressurePa = amplitudeMPa * 1e6;
    return (pressurePa * pressurePa) / (2.0 * density * kSpeedOfSound) / (100.0 * 100.0);
}

double isppa(double amplitudeMPa) {
    return isppa(amplitudeMPa, 1040.0);  // checkSonicationSafety.m's density
}

double mechanicalIndex(double amplitudeMPa, double centerFrequencyMHz) {
    return amplitudeMPa / std::sqrt(centerFrequencyMHz);
}

IsptaResult isptaFromParams(const SonicationSafetyParams& p) {
    const double intensity = isppa(p.amplitudeMPa, 1046.0);  // getISPTAFromStimParams.m's density
    const double numPulseTransmits = std::floor(p.bd / p.pi);
    const double onOverOff = numPulseTransmits * p.pd / p.bd;

    IsptaResult r;
    r.isptaBurstDuration = intensity * onOverOff;
    r.isptaBurstInterval = intensity * onOverOff * p.bd / p.bi;
    return r;
}

}  // namespace beam::safety
