#include "serialcom/command.hpp"

#include <algorithm>
#include <sstream>
#include <utility>

#include <cmath>
#include <cstdio>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace beam::serialcom {

namespace {

// 5 significant digits, trailing zeros trimmed.
std::string num2str(double x) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.5g", x);
    return std::string(buf);
}

// Trims, then parses; NaN when the whole token is not a number.
double str2double(const std::string& s) {
    std::size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    std::size_t end = s.find_last_not_of(" \t\r\n");
    const std::string trimmed = s.substr(start, end - start + 1);

    try {
        std::size_t consumed = 0;
        const double v = std::stod(trimmed, &consumed);
        if (consumed != trimmed.size()) {
            return std::numeric_limits<double>::quiet_NaN();
        }
        return v;
    } catch (...) {
        return std::numeric_limits<double>::quiet_NaN();
    }
}

}  // namespace

std::string setSerialCommandFromStimParams(const SonicationTiming& t, double dutyCycle) {
    std::string cmd = "Stim_PR" + num2str(dutyCycle);
    cmd += "_PD" + num2str(t.pd * 1000.0);
    cmd += "_PI" + num2str(t.pi * 1000.0);
    cmd += "_BD" + num2str(t.bd * 1000.0);
    cmd += "_BI" + num2str(t.bi * 1000.0);
    cmd += "_D" + num2str(t.endTime - t.startTime);
    return cmd;
}

std::string normaliseEcho(const std::string& text) {
    std::string out;
    bool pendingSpace = false;
    for (const char ch : text) {
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
            pendingSpace = !out.empty();
            continue;
        }
        if (pendingSpace) out.push_back(' ');
        pendingSpace = false;
        out.push_back(ch);
    }
    return out;
}

std::string expectedStimEcho(const SonicationTiming& t, double dutyCycle) {
    // 84 MHz over a 8400 prescaler, from the firmware's own timer setup.
    constexpr double kTicksPerMs = 10.0;
    constexpr double kTicksPerSecond = kTicksPerMs * 1000.0;
    // In milliseconds, as the command carries them: subtracting in seconds
    // leaves 0.7 - 0.03 at 0.6699999... and loses a tick.
    const auto ms = [](double seconds) { return std::llround(seconds * 1000.0); };
    const auto ticks = [&ms](double seconds) {
        return static_cast<long long>(ms(seconds) * kTicksPerMs);
    };
    const auto ticksBetween = [&ms](double a, double b) {
        return static_cast<long long>((ms(a) - ms(b)) * kTicksPerMs);
    };
    char buf[160];
    std::snprintf(buf, sizeof(buf),
                  "PR:%lld.000 PD:%lld.000 PI:%lld.000 BD:%lld.000 BI:%lld.000 D:%lld.000",
                  static_cast<long long>(dutyCycle * 100.0), ticks(t.pd),
                  ticksBetween(t.pi, t.pd), ticks(t.bd), ticksBetween(t.bi, t.bd),
                  static_cast<long long>((t.endTime - t.startTime) * kTicksPerSecond));
    return buf;
}

namespace {

// "PR:67.000 PD:50.000 ..." -> name/value pairs, in order.
std::vector<std::pair<std::string, double>> echoFields(const std::string& text) {
    std::vector<std::pair<std::string, double>> fields;
    std::istringstream stream(text);
    std::string token;
    while (stream >> token) {
        const std::size_t colon = token.find(':');
        if (colon == std::string::npos) continue;
        try {
            fields.emplace_back(token.substr(0, colon), std::stod(token.substr(colon + 1)));
        } catch (const std::exception&) {
        }
    }
    return fields;
}

}  // namespace

std::vector<std::string> stimEchoDifferences(const std::string& expected, const std::string& actual,
                                             double relativeTolerance) {
    const auto want = echoFields(expected);
    const auto got = echoFields(actual);
    std::vector<std::string> differences;
    if (want.size() != got.size()) {
        differences.push_back("field count");
        return differences;
    }
    for (std::size_t i = 0; i < want.size(); ++i) {
        if (want[i].first != got[i].first) {
            differences.push_back(want[i].first + "/" + got[i].first);
            continue;
        }
        const double allowed = std::max(1.0, std::fabs(want[i].second) * relativeTolerance);
        if (std::fabs(want[i].second - got[i].second) <= allowed) continue;
        char buf[96];
        std::snprintf(buf, sizeof(buf), "%s sent %.0f ack %.0f", want[i].first.c_str(),
                      want[i].second, got[i].second);
        differences.emplace_back(buf);
    }
    return differences;
}

CorrectionReceive parseCorrectionReceiveString(const std::string& response) {
    std::vector<std::string> fields;
    std::stringstream ss(response);
    std::string field;
    while (std::getline(ss, field, ',')) {
        fields.push_back(field);
    }
    if (fields.size() < 5) {
        throw std::runtime_error("parseCorrectionReceiveString: expected 5 comma-separated fields, got " +
                                  std::to_string(fields.size()));
    }

    CorrectionReceive r;
    r.ch0Max = str2double(fields[0]);
    r.ch1Max = str2double(fields[1]);
    r.rawValue = str2double(fields[2]);
    r.attenuation = str2double(fields[3]);
    r.calibrationVal = str2double(fields[4]);
    return r;
}

}  // namespace beam::serialcom
