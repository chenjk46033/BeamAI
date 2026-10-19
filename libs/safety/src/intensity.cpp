#include "safety/intensity.hpp"

#include <stdexcept>

#include "stimulation/events.hpp"

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

double isptaAll(const SonicationSafetyParams& p) {
    const double duration = p.endTime - p.startTime;
    if (duration <= 0.0 || p.bi <= 0.0 || p.bd <= 0.0 || p.pi <= 0.0) return 0.0;
    beam::stimulation::SonicationSchedule schedule;
    schedule.startTime = p.startTime;
    schedule.endTime = p.endTime;
    schedule.bi = p.bi;
    schedule.bd = p.bd;
    schedule.pi = p.pi;
    schedule.pd = p.pd;

    std::vector<beam::stimulation::TimelineSegment> timeline;
    try {
        timeline = beam::stimulation::computeSonicationEventTimeline({schedule});
    } catch (const std::exception&) {
        // An invalid schedule, or overlapping bursts. The source leaves its
        // output unassigned here.
        return 0.0;
    }

    const double intensity = isppa(p.amplitudeMPa, 1046.0);
    const double numPulseTransmits = std::floor(p.bd / p.pi);
    const double onOverOff = numPulseTransmits * p.pd / p.bd;
    double total = 0.0;
    for (const beam::stimulation::TimelineSegment& segment : timeline) {
        // `if eventVector(j).txNum ~= 0`: the bursts, not the gaps between
        // them.
        if (segment.txNum == 0) continue;
        total += intensity * (segment.timeOff - segment.timeOn) * onOverOff;
    }
    return total / duration;
}

}  // namespace beam::safety
