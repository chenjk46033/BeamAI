#include "safety/checks.hpp"

#include <string>

#include "safety/intensity.hpp"
#include "safety/limits.hpp"

namespace beam::safety {

namespace {

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
            report.pass = false;
            report.messages.push_back("Stimulation " + std::to_string(i + 1) + " ISPPA " + num(intensity) +
                                       " above ISPPA limit of " + num(kIsppaThresholdWPerCm2) + " (W/cm^2)");
        }
    }

    // --- parameter validity per sonication ---
    for (int i = 0; i < n; ++i) {
        const SonicationSafetyParams& p = sonications[static_cast<std::size_t>(i)];
        const double duration = p.endTime - p.startTime;
        const std::string base = "Invalid Parameter- Stimulation " + std::to_string(i + 1) + ": ";
        if (p.pd > p.pi) { report.pass = false; report.messages.push_back(base + "PD > PI"); }
        if (p.bd > p.bi) { report.pass = false; report.messages.push_back(base + "BD > BI"); }
        if (p.pi > p.bd) { report.pass = false; report.messages.push_back(base + "PI > BD"); }
        if (p.bi > duration) { report.pass = false; report.messages.push_back(base + "BI > Duration"); }
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
        if (ispta.isptaBurstInterval > kIsptaThresholdWPerCm2) {
            report.pass = false;
            report.messages.push_back("Stimulation " + std::to_string(i + 1) + ": Burst Parameters ISPTA " +
                                       num(ispta.isptaBurstInterval) + " Exceed ISPTA Limit of " +
                                       num(kIsptaThresholdWPerCm2) + " (W/cm^2)");
        }
        if (allExceedMi) {
            report.pass = false;
            report.messages.push_back("Stimulation " + std::to_string(i + 1) +
                                       ": Mechanical Index Exceed Limit of " + num(kMechanicalIndexThreshold));
        }
    }

    return report;
}

SafetyReport checkCouplingSafety(double throughTransmitAtt) {
    SafetyReport report;
    if (throughTransmitAtt < 0.1) {
        report.pass = false;
        report.messages.push_back("WARNING: Coupling transmission is abnormally low");
    }
    return report;
}

}  // namespace beam::safety
