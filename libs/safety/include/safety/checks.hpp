#pragma once

#include <string>
#include <vector>

#include "safety/intensity.hpp"

namespace beam::safety {

struct SafetyReport {
    bool pass = true;
    std::vector<std::string> messages;
};

// Port of the pure-computation part of checkSonicationSafety.m: per
// sonication, checks ISPPA against 190 W/cm^2, the PD/PI/BD/BI/duration
// parameter-validity relations, and (only if those pass) the burst-interval
// ISPTA against 0.720 W/cm^2 and the mechanical index against 1.9.
//
// NOT included (they read GUI/session state, not sonication parameters):
// the ISPTA-over-all-events check (needs setBurstEventsFromStimParams), the
// "registration incomplete" and "through-transmit too low" preconditions,
// and every `set(app.SonicateButton, ...)` / report-text side effect. The
// caller adds those.
SafetyReport checkSonicationParameters(const std::vector<SonicationSafetyParams>& sonications);

// Port of checkCouplingSafety.m's one real check: through-transmit
// attenuation below 0.1 is abnormally low.
SafetyReport checkCouplingSafety(double throughTransmitAtt);

}  // namespace beam::safety
