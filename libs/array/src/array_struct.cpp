#include "array/array_struct.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

#include "array/geometry.hpp"
#include "util/geometry_math.hpp"

namespace beam::array {

std::pair<int, std::vector<int>> getReceiveElements(const Eigen::MatrixXd& rect, int elIndex1Based,
                                                      double lambda, double diameter) {
    const int col = elIndex1Based - 1;
    const Eigen::Vector3d c1 = rectCorner(rect, col, 1);
    const Eigen::Vector3d c2 = rectCorner(rect, col, 2);
    const Eigen::Vector3d c3 = rectCorner(rect, col, 3);
    const Eigen::Vector3d center = rectCenter(rect, col);
    const Eigen::Vector3d n = normalVectorFrom3Points(c1, c2, c3);

    double maxDist = std::numeric_limits<double>::infinity();
    int opposingElement = 0;
    const Eigen::Index numCols = rect.cols();
    for (Eigen::Index i = 0; i < numCols; ++i) {
        const int i1Based = static_cast<int>(i) + 1;
        if (i1Based == elIndex1Based || std::abs(i1Based - elIndex1Based) < 28) {
            continue;
        }
        const double d = util::distancePointToLine(n, center, rectCenter(rect, i));
        if (d < maxDist) {
            maxDist = d;
            opposingElement = i1Based;
        }
    }
    if (opposingElement == 0) {
        throw std::runtime_error(
            "getReceiveElements: no opposing element found -- rect has too few columns for the "
            ">=28-apart exclusion (matches an implicit assumption in the MATLAB source)");
    }

    const Eigen::Vector3d opposingCenter = rectCenter(rect, opposingElement - 1);
    const double z = (center - opposingCenter).norm();
    const double R = 2.0 * (lambda * z / diameter);

    std::vector<int> receiveElements;
    for (Eigen::Index i = 0; i < numCols; ++i) {
        const double distance = (rectCenter(rect, i) - opposingCenter).cwiseAbs().sum();
        if (distance < R) {
            receiveElements.push_back(static_cast<int>(i) + 1);
        }
    }
    return {opposingElement, receiveElements};
}

ArrayStruct defineArrayStruct(const Eigen::MatrixXd& rect, double frequency,
                               std::array<double, 2> elementDimensions) {
    ArrayStruct arrayStruct;
    arrayStruct.rect = rect;
    arrayStruct.frequency = frequency;
    arrayStruct.elementDimensions = elementDimensions;

    const Eigen::Index n = rect.cols();
    arrayStruct.element.resize(static_cast<size_t>(n));

    const double lambda = 1490.0 / frequency;
    const double diameter = std::max(elementDimensions[0], elementDimensions[1]);

    for (Eigen::Index i = 0; i < n; ++i) {
        const int col1Based = static_cast<int>(i) + 1;
        ArrayElement& el = arrayStruct.element[static_cast<size_t>(i)];
        el.number = static_cast<int>(std::lround(rect(kRectElementNumberRow, i)));
        el.position = rectCenter(rect, static_cast<int>(i));
        el.corners.col(0) = rectCorner(rect, static_cast<int>(i), 1);
        el.corners.col(1) = rectCorner(rect, static_cast<int>(i), 2);
        el.corners.col(2) = rectCorner(rect, static_cast<int>(i), 3);
        el.corners.col(3) = rectCorner(rect, static_cast<int>(i), 4);

        auto [opposing, receive] = getReceiveElements(rect, col1Based, lambda, diameter);
        el.opposingElement = opposing;
        el.receiveElements = std::move(receive);
    }

    calculateArrayNormalVectors(arrayStruct);
    return arrayStruct;
}

std::vector<int> getOpposingElements(const ArrayData& arrayData, int elementNumber) {
    // Which array holds `elementNumber`? MATLAB keeps scanning and lets the
    // last match win, so this does too (findsMatch guards the not-found case
    // the source leaves as an unassigned-variable runtime error).
    bool found = false;
    size_t currentArray = 0;
    for (size_t i = 0; i < arrayData.array.size(); ++i) {
        for (const ArrayElement& el : arrayData.array[i].element) {
            if (el.number == elementNumber) {
                currentArray = i;
                found = true;
            }
        }
    }
    if (!found) {
        throw std::invalid_argument("getOpposingElements: element number " +
                                    std::to_string(elementNumber) +
                                    " not present in any array");
    }

    std::vector<int> opElements;
    for (size_t i = 0; i < arrayData.array.size(); ++i) {
        if (i == currentArray) {
            continue;
        }
        const Eigen::MatrixXd& rect = arrayData.array[i].rect;
        for (Eigen::Index c = 0; c < rect.cols(); ++c) {
            opElements.push_back(static_cast<int>(std::lround(rect(kRectElementNumberRow, c))));
        }
    }
    return opElements;
}

}  // namespace beam::array
