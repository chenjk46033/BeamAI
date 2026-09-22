#include "serialcom/command.hpp"

#include <cmath>
#include <cstdio>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace beam::serialcom {

namespace {

// Approximates MATLAB num2str(x) (no format arg) for scalar doubles:
// 5 significant digits, trailing zeros trimmed -- "%.5g".
std::string num2str(double x) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.5g", x);
    return std::string(buf);
}

// str2double(s): trims, parses a double, or NaN if the whole token isn't a
// number (matching MATLAB).
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
