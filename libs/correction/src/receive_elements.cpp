#include "correction/receive_elements.hpp"

#include <stdexcept>
#include <string>

#include "array/array_struct.hpp"
#include "util/geometry_math.hpp"

namespace beam::correction {

std::vector<int> getReceiveElementsUnderAngle(const beam::array::ArrayData& arrayData, int eli1Based,
                                               std::optional<Eigen::Vector3d> targetPosMm,
                                               double angleThreshold) {
    const beam::array::ArrayStruct& array = arrayData.arrayTotal;
    const auto elemAt1Based = [&array](int n1Based) -> const beam::array::ArrayElement& {
        if (n1Based < 1 || static_cast<size_t>(n1Based) > array.element.size()) {
            throw std::out_of_range("getReceiveElementsUnderAngle: element number " +
                                    std::to_string(n1Based) + " out of range");
        }
        return array.element[static_cast<size_t>(n1Based - 1)];
    };

    const beam::array::ArrayElement& eli = elemAt1Based(eli1Based);

    Eigen::Vector3d vjt;
    if (targetPosMm) {
        // addVectors(targetPos, -1*position*1000): both operands in mm.
        vjt = beam::util::addVectors(*targetPosMm, -1000.0 * eli.position);
    } else {
        vjt = eli.normalVector;
    }

    const std::vector<int> opElements = beam::array::getOpposingElements(arrayData, eli1Based);

    std::vector<int> receiveElements;
    for (int opNum : opElements) {
        const Eigen::Vector3d vjm = elemAt1Based(opNum).position - eli.position;
        double phi = beam::util::angleBetweenTwoVectors(vjm, vjt);
        const double phiNeg = beam::util::angleBetweenTwoVectors(vjm, -vjt);
        if (phiNeg < phi) {
            phi = phiNeg;
        }
        if (phi < angleThreshold) {
            receiveElements.push_back(opNum);
        }
    }
    return receiveElements;
}

}  // namespace beam::correction
