#include "gui/safety_presenter.hpp"

#include "safety/limits.hpp"

namespace beam::gui {

SonicationSafetyReport checkSonicationSafety(
    const std::vector<beam::safety::SonicationSafetyParams>& sonications,
    const SonicationSafetyPreconditions& pre) {
    SonicationSafetyReport report;

    // --- per-sonication parameter checks (libs/safety) ---
    const beam::safety::SafetyReport params = beam::safety::checkSonicationParameters(sonications);
    report.pass = params.pass;
    report.messages = params.messages;

    // --- registration precondition ---
    // checkSonicationSafety.m: fails unless BOTH flags are set. Wording
    // tightened into two sentences per the user's explicit ask ("typos ...
    // the message should read") -- the source's own string is one
    // comma-spliced sentence ('Registration incomplete, please complete
    // registration tab'); this port's phrasing is a disclosed departure,
    // not a parity bug.
    if (!(pre.currentRegistrationComplete && pre.mriRegistrationComplete)) {
        report.pass = false;
        report.messages.emplace_back("Registration incomplete. Please complete registration tab.");
    }

    // --- coupling precondition ---
    if (pre.throughTransmitAtt < pre.attenuationThreshold) {
        report.pass = false;
        report.messages.emplace_back("Through transmit too low, please complete correction tab.");
    }

    // app.SystemStatusTextArea.Value
    if (report.messages.empty()) {
        report.statusText = "Sonication Online";
    } else {
        for (std::size_t i = 0; i < report.messages.size(); ++i) {
            if (i != 0) {
                report.statusText += "\n";
            }
            report.statusText += report.messages[i];
        }
    }

    return report;
}

}  // namespace beam::gui
