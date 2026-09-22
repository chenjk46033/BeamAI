#include "stimulation/stim_freqs.hpp"

#include <stdexcept>

namespace beam::stimulation {

Eigen::VectorXd defineStimFreqs(const std::string& control, int arrayNumElements) {
    if (control == "650") {
        return Eigen::VectorXd::Constant(arrayNumElements, 0.65);
    }
    if (control == "high") {
        return Eigen::VectorXd::Constant(arrayNumElements, 0.7);
    }
    if (control == "MFS252" || control == "MFS21") {
        throw std::invalid_argument("defineStimFreqs: '" + control +
                                    "' loads a hardcoded .mat file that is not ported");
    }
    throw std::invalid_argument("defineStimFreqs: undefined control value '" + control + "'");
}

}  // namespace beam::stimulation
