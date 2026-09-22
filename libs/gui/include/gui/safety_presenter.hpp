#pragma once

#include <string>
#include <vector>

#include "safety/checks.hpp"
#include "safety/intensity.hpp"

// GUI phase, Safety-tab slice: the orchestration in
// BeamV0/GUIMatlab/BEAM/GUI/Safety/checkSonicationSafety.m that sits on top
// of the already-ported acoustic-dose math (libs/safety). No Qt dependency
// -- this is the presenter; libs/gui_qt renders its output.
//
// libs/safety already ports the per-sonication checks
// (checkSonicationParameters: ISPPA, PD/PI/BD/BI validity, burst-interval
// ISPTA, MI). This layer adds the parts of checkSonicationSafety.m that
// read GUI/session state: the registration-complete and
// through-transmit-attenuation preconditions, and the final status string
// ("Sonication Online" vs. the assembled report).

namespace beam::gui {

// The GUI/session state checkSonicationSafety.m reads off `app` beyond the
// per-sonication parameters:
//   app.sys.frame.CurrentRegistrationComplete / .MRIRegistrationComplete
//   app.sys.RTT(1).att  vs  app.attenuationThreshold
struct SonicationSafetyPreconditions {
    bool currentRegistrationComplete = false;
    bool mriRegistrationComplete = false;
    double throughTransmitAtt = 1.0;
    double attenuationThreshold = 0.5;
};

struct SonicationSafetyReport {
    bool pass = true;
    std::vector<std::string> messages;
    // app.SystemStatusTextArea.Value: "Sonication Online" when messages is
    // empty, otherwise the messages themselves (joined with newlines here).
    std::string statusText;
};

// Port of checkSonicationSafety.m's overall flow, minus the
// `set(app.SonicateButton,'Enable',...)` side effects and the deferred
// IsptaAll-over-all-events check (needs setBurstEventsFromStimParams; see
// docs/known_gaps_stimulation.md). Runs
// beam::safety::checkSonicationParameters first, then appends the
// registration-incomplete and through-transmit-too-low messages, matching
// the source's order and its "any failure clears the pass flag" rule.
SonicationSafetyReport checkSonicationSafety(
    const std::vector<beam::safety::SonicationSafetyParams>& sonications,
    const SonicationSafetyPreconditions& preconditions);

}  // namespace beam::gui
