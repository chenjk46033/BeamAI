#include "gui/sonicate_orchestrator.hpp"

#include <Eigen/Core>

#include "stimulation/duty_cycle.hpp"

namespace beam::gui {

namespace {

// generalSonicateMaster.m's hardcoded calibration curve (duty-cycle % vs.
// pressure, MPa) -- not app-configurable in the source, so not exposed as
// a parameter here either.
const Eigen::VectorXd& calDutyCurve() {
    static const Eigen::VectorXd v = [] {
        Eigen::VectorXd x(7);
        x << 40, 45, 56, 75, 80, 90, 97.5;
        return x;
    }();
    return v;
}

const Eigen::VectorXd& calPressureCurve() {
    static const Eigen::VectorXd v = [] {
        Eigen::VectorXd x(7);
        x << 0.75, 1.12, 1.72, 2.95, 3.1, 3.72, 3.92;
        return x;
    }();
    return v;
}

}  // namespace

SonicateOutcome prepareSonication(const beam::safety::SonicationSafetyParams& params,
                                   double transmissionPeak2Peak, double couplingThreshold,
                                   bool waitForExternalTrigger) {
    SonicateOutcome out;
    out.waitForTrigger = waitForExternalTrigger;
    out.durationSeconds = params.endTime - params.startTime;
    out.dutyCycle = beam::stimulation::pressureToDutyCycleGivenTransmission(
        params.amplitudeMPa, transmissionPeak2Peak, calDutyCurve(), calPressureCurve());

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
