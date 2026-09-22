#include "stimulation/apodization.hpp"

namespace beam::stimulation {

ApodResult getApodFromAtt(double vAmplitudeToMPa, Eigen::VectorXd att, double threshAmpCorrection) {
    for (Eigen::Index i = 0; i < att.size(); ++i) {
        if (att(i) < threshAmpCorrection) {
            att(i) = threshAmpCorrection;
        }
    }
    const Eigen::VectorXd amplitudes = vAmplitudeToMPa / att.array();

    ApodResult result;
    result.v = amplitudes.maxCoeff();
    result.apods = amplitudes / result.v;
    return result;
}

}  // namespace beam::stimulation
