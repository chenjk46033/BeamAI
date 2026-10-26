#include "gui/sonicate_orchestrator.hpp"

#include <Eigen/Core>

#include "stimulation/duty_cycle.hpp"

namespace beam::gui {

SonicateOutcome prepareSonication(const beam::safety::SonicationSafetyParams& params,
                                   double transmissionPeak2Peak, double couplingThreshold,
                                   bool waitForExternalTrigger,
                                   const beam::stimulation::CalibrationCurve& calibration) {
    SonicateOutcome out;
    out.waitForTrigger = waitForExternalTrigger;
    out.durationSeconds = params.endTime - params.startTime;
    out.dutyCycle = beam::stimulation::pressureToDutyCycleGivenTransmission(
        params.amplitudeMPa, transmissionPeak2Peak, calibration.dutyPercent, calibration.pressureMpa);

    bool finalCheck = true;
    if (transmissionPeak2Peak < couplingThreshold) {
        out.messages.push_back("Transmission below coupling threshold");
        finalCheck = false;
    }
    // Note: pressureToDutyCycleGivenTransmission clamps to [0.4, 0.75], so
    // this check (ported verbatim from the source) can never actually
    // trigger -- a preserved quirk, not a bug introduced here.
    if (out.dutyCycle > 0.90) {
        out.messages.push_back("Input pressure too high");
        finalCheck = false;
    }
    out.started = finalCheck;
    return out;
}

beam::safety::SonicationSafetyParams stimParamsFromTableRow(double pd, double pi, double bd, double bi,
                                                              double amplitudeMPa, double startTime,
                                                              double endTime) {
    beam::safety::SonicationSafetyParams p;
    p.amplitudeMPa = amplitudeMPa;
    p.centerFrequencyMHz = 0.300;  // setStimParamsFromApp.m's hardcoded constant
    p.pd = pd;
    p.pi = pi;
    p.bd = bd;
    p.bi = bi;
    p.startTime = startTime;
    p.endTime = endTime;
    return p;
}

}  // namespace beam::gui
