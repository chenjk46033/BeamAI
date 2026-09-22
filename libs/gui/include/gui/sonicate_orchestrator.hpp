#pragma once

#include <string>
#include <vector>

#include "safety/intensity.hpp"

// Ported from the decision logic in
// BeamV0/GUIMatlab/BEAM/Stimulation/GeneralSonication/generalSonicateMaster.m:
// the two last-moment checks made right before a real sonication command
// would be sent (transmission below the coupling threshold; the computed
// duty cycle too high), the Immediate/External trigger toggle
// (`app.TriggerSwitch.Value`), and the duty-cycle computation itself
// (already-ported `pressureToDutyCycleGivenTransmission`, with the
// source's hardcoded calibration curve).
//
// Deliberately not ported here: `testModeFlag` (hardcoded 0 in the source
// -- dead, its branch never runs); `startStandaloneCountdown` /
// `setShamAudio` (UI countdown timer + sham-audio playback -- app-state/
// hardware glue with no effect on whether/what gets sent; see
// docs/known_gaps_gui.md and docs/known_gaps_serialcom_sham.md);
// `app.sys.stimParams` / `app.sys.log` bookkeeping (session-state, not
// wired up anywhere yet); and the actual serial send
// (`setSerialCommandFromStimParams` + `sendSerialCommand`, both already
// ported in libs/serialcom) -- that, and choosing a COM port, is the
// caller's job, since this function has no hardware dependency.

namespace beam::gui {

struct SonicateOutcome {
    bool started = false;               // generalSonicateMaster.m's finalCheckFlag
    std::vector<std::string> messages;  // failure reasons; both can appear (not short-circuited, matching source)
    double dutyCycle = 0.0;             // fraction in [0.4, 0.75] (see pressureToDutyCycleGivenTransmission), 0 if not started
    bool waitForTrigger = false;        // stimParams(1).waitForTrigger
    double durationSeconds = 0.0;       // stimParams(1).endTime - startTime
};

// params: the sonication to fire (amplitudeMPa/centerFrequencyMHz/pd/pi/bd/
// bi/startTime/endTime -- the same fields setStimParamsFromApp.m copies out
// of one stimParamTable row). transmissionPeak2Peak: the through-transmit
// coupling measurement (beam::correction::throughTransmitAmplitude's `amp`).
// couplingThreshold: app.sys.RTT(1).couplingThreshold (the caller's own
// constant; this project's demo reuses the Correction tab's threshold).
// waitForExternalTrigger: the trigger-mode toggle's current value.
SonicateOutcome prepareSonication(const beam::safety::SonicationSafetyParams& params,
                                   double transmissionPeak2Peak, double couplingThreshold,
                                   bool waitForExternalTrigger);

// Port of setStimParamsFromApp.m's per-row field copy (PD/PI/BD/BI/
// Amplitude/startTime/endTime, plus the hardcoded centerFrequencyMHz =
// 0.300 constant) -- what the source calls once per stimParamTable row to
// build the stimParams array `prepareSonication` above ultimately consumes.
// Not carried: the source's own `txElements = [1,2]`, `DC = 0`, `att = 0`
// fields -- boilerplate defaults on a MATLAB struct field this port's
// pipeline never reads back (the real tx-element list comes from array
// geometry, not this table; duty cycle and attenuation are computed
// downstream, not read from here).
beam::safety::SonicationSafetyParams stimParamsFromTableRow(double pd, double pi, double bd, double bi,
                                                              double amplitudeMPa, double startTime,
                                                              double endTime);

}  // namespace beam::gui
