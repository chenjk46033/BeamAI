#include "stimulation/calibration_curve.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace beam::stimulation {

namespace {

bool parsePair(const std::string& line, double* duty, double* pressure) {
    std::istringstream row(line);
    std::string left;
    std::string right;
    if (!std::getline(row, left, ',')) return false;
    if (!std::getline(row, right, ',')) return false;
    try {
        std::size_t usedLeft = 0;
        std::size_t usedRight = 0;
        const double a = std::stod(left, &usedLeft);
        const double b = std::stod(right, &usedRight);
        if (usedLeft == 0 || usedRight == 0) return false;
        *duty = a;
        *pressure = b;
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

}  // namespace

const CalibrationCurve& defaultCalibrationCurve() {
    static const CalibrationCurve curve = [] {
        CalibrationCurve c;
        c.dutyPercent.resize(7);
        c.pressureMpa.resize(7);
        c.dutyPercent << 40, 45, 56, 75, 80, 90, 97.5;
        c.pressureMpa << 0.75, 1.12, 1.72, 2.95, 3.1, 3.72, 3.92;
        return c;
    }();
    return curve;
}

CalibrationCurve readCalibrationCurve(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("calibration curve: cannot open " + path);

    std::vector<double> duty;
    std::vector<double> pressure;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        double a = 0.0;
        double b = 0.0;
        // A header line simply fails to parse, which is how it is skipped.
        if (!parsePair(line, &a, &b)) continue;
        duty.push_back(a);
        pressure.push_back(b);
    }

    CalibrationCurve curve;
    curve.dutyPercent = Eigen::Map<Eigen::VectorXd>(duty.data(), Eigen::Index(duty.size()));
    curve.pressureMpa = Eigen::Map<Eigen::VectorXd>(pressure.data(), Eigen::Index(pressure.size()));
    if (!curve.usable())
        throw std::runtime_error("calibration curve: " + path + " holds fewer than two points");
    return curve;
}

void writeCalibrationCurve(const std::string& path, const CalibrationCurve& curve) {
    std::ofstream out(path);
    if (!out) throw std::runtime_error("calibration curve: cannot write " + path);
    out << "duty_percent,pressure_mpa\n";
    for (Eigen::Index i = 0; i < curve.dutyPercent.size(); ++i)
        out << curve.dutyPercent(i) << ',' << curve.pressureMpa(i) << '\n';
}

}  // namespace beam::stimulation
