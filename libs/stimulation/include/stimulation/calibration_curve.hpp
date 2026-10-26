#pragma once

// The transducer's duty-cycle against pressure. Hardcoded in BeamV0; a file
// here, because it is a property of the hardware and not of the software.

#include <string>

#include <Eigen/Core>

namespace beam::stimulation {

struct CalibrationCurve {
    Eigen::VectorXd dutyPercent;
    Eigen::VectorXd pressureMpa;

    bool usable() const {
        return dutyPercent.size() >= 2 && dutyPercent.size() == pressureMpa.size();
    }
};

// The values BeamV0 carried inline, used when no file is present.
const CalibrationCurve& defaultCalibrationCurve();

// Reads "duty_percent,pressure_mpa" rows; a leading non-numeric line is taken
// as a header. Throws std::runtime_error when the file is missing or unusable.
CalibrationCurve readCalibrationCurve(const std::string& path);

// Writes the curve in the format readCalibrationCurve expects.
void writeCalibrationCurve(const std::string& path, const CalibrationCurve& curve);

}  // namespace beam::stimulation
