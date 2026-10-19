#include "safety/checks.hpp"

#include <string>

#include "safety/intensity.hpp"
#include "safety/limits.hpp"

namespace beam::safety {

namespace {

// Keeps the two lists in step: nothing adds a message without saying which
// rule it came from.
void fail(SafetyReport& report, int index, ViolationKind kind, const std::string& message) {
    report.pass = false;
    // The number first: it is what gets quoted, written down and searched
    // for, and the sentence after it is the same sentence BeamV0 shows.
    report.messages.push_back("[Rule " + std::to_string(ruleNumber(kind)) + "] " + message);
    report.violations.push_back(Violation{index, kind});
}

std::string num(double v) {
    // Human-readable only; not a byte-match of MATLAB num2str.
    std::string s = std::to_string(v);
    const auto dot = s.find('.');
    if (dot != std::string::npos) {
        auto last = s.find_last_not_of('0');
        if (last == dot) {
            last -= 1;
        }
        s.erase(last + 1);
    }
    return s;
}

}  // namespace

SafetyReport checkSonicationParameters(const std::vector<SonicationSafetyParams>& sonications) {
    SafetyReport report;
    const int n = static_cast<int>(sonications.size());

    // --- ISPPA per sonication ---
    for (int i = 0; i < n; ++i) {
        const double intensity = isppa(sonications[static_cast<std::size_t>(i)].amplitudeMPa);
        if (intensity > kIsppaThresholdWPerCm2) {
            fail(report, i, ViolationKind::kIsppa,
                 "Stimulation " + std::to_string(i + 1) + " ISPPA " + num(intensity) +
                     " above ISPPA limit of " + num(kIsppaThresholdWPerCm2) + " (W/cm^2)");
        }
    }

    // --- parameter validity per sonication ---
    for (int i = 0; i < n; ++i) {
        const SonicationSafetyParams& p = sonications[static_cast<std::size_t>(i)];
        const double duration = p.endTime - p.startTime;
        const std::string base = "Invalid Parameter- Stimulation " + std::to_string(i + 1) + ": ";
        if (p.pd > p.pi)
            fail(report, i, ViolationKind::kPulseDurationOverInterval, base + "PD > PI");
        if (p.bd > p.bi)
            fail(report, i, ViolationKind::kBurstDurationOverInterval, base + "BD > BI");
        if (p.pi > p.bd)
            fail(report, i, ViolationKind::kPulseIntervalOverBurst, base + "PI > BD");
        if (p.bi > duration)
            fail(report, i, ViolationKind::kBurstIntervalOverDuration, base + "BI > Duration");
    }

    // The source runs the ISPTA / MI block only if nothing has failed yet.
    if (!report.pass) {
        return report;
    }

    // checkSonicationSafety.m: `MI = getMechanicalIndex(app)` is a vector,
    // and `if MI > MIthreshold` is only true when *every* element exceeds
    // the limit (MATLAB `if` on a vector = all()). Preserved as-is.
    bool allExceedMi = n > 0;
    for (int i = 0; i < n; ++i) {
        const SonicationSafetyParams& p = sonications[static_cast<std::size_t>(i)];
        if (mechanicalIndex(p.amplitudeMPa, p.centerFrequencyMHz) <= kMechanicalIndexThreshold) {
            allExceedMi = false;
        }
    }

    for (int i = 0; i < n; ++i) {
        const SonicationSafetyParams& p = sonications[static_cast<std::size_t>(i)];
        const IsptaResult ispta = isptaFromParams(p);
        // The source checks IsptaAll first, then the burst-interval one.
        const double all = isptaAll(p);
        if (all > kIsptaThresholdWPerCm2) {
            fail(report, i, ViolationKind::kIsptaAll,
                 "Stimulation " + std::to_string(i + 1) + ": ISPTA " + num(all) +
                     " Exceeds ISPTA Limit of " + num(kIsptaThresholdWPerCm2));
        }
        if (ispta.isptaBurstInterval > kIsptaThresholdWPerCm2) {
            fail(report, i, ViolationKind::kIsptaBurstInterval,
                 "Stimulation " + std::to_string(i + 1) + ": Burst Parameters ISPTA " +
                     num(ispta.isptaBurstInterval) + " Exceed ISPTA Limit of " +
                     num(kIsptaThresholdWPerCm2) + " (W/cm^2)");
        }
        if (allExceedMi) {
            fail(report, i, ViolationKind::kMechanicalIndex,
                 "Stimulation " + std::to_string(i + 1) + ": Mechanical Index " +
                     num(mechanicalIndex(p.amplitudeMPa, p.centerFrequencyMHz)) +
                     " Exceed Limit of " + num(kMechanicalIndexThreshold));
        }
    }

    return report;
}

SafetyReport checkCouplingSafety(double throughTransmitAtt) {
    SafetyReport report;
    if (throughTransmitAtt < 0.1) {
        report.pass = false;
        report.messages.push_back("WARNING: Coupling transmission is abnormally low");
        // No sonication row to blame: this one is about the measurement.
    }
    return report;
}

}  // namespace beam::safety
