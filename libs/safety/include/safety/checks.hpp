#pragma once

#include <string>
#include <vector>

#include "safety/intensity.hpp"

namespace beam::safety {

// Which rule a message is about. The message says it in the source's own
// words, for the operator; this says it in a form a caller can act on --
// marking the cells that caused it, for instance -- without parsing English.
//
// The numbers are the rule's identity and are written on screen and into
// the log, so they are fixed: a rule keeps its number for good, a retired
// one leaves a hole rather than being reused, and a new one takes the next
// free number in its band. The bands group them -- 1xx the acoustic limits,
// 2xx the schedule relations, 3xx the session preconditions, 4xx the
// coupling measurement -- and are
// spaced so a rule can be added beside the ones it belongs with.
enum class ViolationKind {
    kIsppa = 110,                      // ISPPA over its limit
    kIsptaBurstInterval = 120,         // burst-interval ISPTA over its limit
    kIsptaAll = 130,                   // whole-sonication ISPTA over its limit
    kMechanicalIndex = 140,            // mechanical index over its limit
    kPulseDurationOverInterval = 210,  // PD > PI
    kBurstDurationOverInterval = 220,  // BD > BI
    kPulseIntervalOverBurst = 230,     // PI > BD
    kBurstIntervalOverDuration = 240,  // BI > Duration
    kCouplingTooLow = 410,             // through-transmit below 0.1
};

// The number as written, e.g. 110. Preconditions the caller adds have their
// own numbers in the 3xx band; see the callers for those.
inline int ruleNumber(ViolationKind kind) { return static_cast<int>(kind); }

struct Violation {
    int sonicationIndex = 0;  // 0-based row of the input vector
    ViolationKind kind = ViolationKind::kIsppa;
};

struct SafetyReport {
    bool pass = true;
    std::vector<std::string> messages;
    // One per message, in the same order.
    std::vector<Violation> violations;
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
