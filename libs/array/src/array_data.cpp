#include "array/array_data.hpp"

#include <array>
#include <numeric>
#include <stdexcept>

#include "array/array_struct.hpp"

namespace beam::array {

ArrayData defineArrayData(const Eigen::MatrixXd& rect) {
    const ArrayStruct arrayStruct = defineArrayStruct(rect, 150000.0, {0.06, 0.06});

    ArrayData arrayData;
    arrayData.arrayTotal = arrayStruct;
    arrayData.array[0] = arrayStruct;
    arrayData.array[1] = arrayStruct;
    arrayData.array[0].elementMapping = 1;
    arrayData.array[1].elementMapping = 2;
    return arrayData;
}

void reconstructPhysicalArrayHalves(ArrayData& data) {
    const Eigen::MatrixXd& total = data.arrayTotal.rect;
    const double midline = total.row(kRectCenterStartRow).mean();

    std::array<std::vector<Eigen::Index>, 2> columns;
    for (Eigen::Index c = 0; c < total.cols(); ++c) {
        // MATLAB designation 1 is subject-Right, designation 2 is subject-Left.
        columns[total(kRectCenterStartRow, c) >= midline ? 0 : 1].push_back(c);
    }
    if (columns[0].empty() || columns[1].empty()) {
        throw std::runtime_error(
            "reconstructPhysicalArrayHalves: geometry does not contain two distinguishable panels");
    }

    for (std::size_t half = 0; half < 2; ++half) {
        Eigen::MatrixXd rect(total.rows(), static_cast<Eigen::Index>(columns[half].size()));
        for (Eigen::Index c = 0; c < rect.cols(); ++c) {
            rect.col(c) = total.col(columns[half][static_cast<std::size_t>(c)]);
        }
        data.array[half] = defineArrayStruct(rect, data.arrayTotal.frequency,
                                             data.arrayTotal.elementDimensions);
        data.array[half].elementMapping = static_cast<int>(half) + 1;
    }
}

namespace {
std::vector<int> range(int first, int last) {
    std::vector<int> out(static_cast<size_t>(last - first + 1));
    std::iota(out.begin(), out.end(), first);
    return out;
}
}  // namespace

TxElements defineArrayTxElements(const std::string& mode) {
    TxElements result;
    if (mode == "first") {
        result.txElements = {range(1, 126)};
        result.txElementsArray = {range(1, 126)};
    } else if (mode == "second") {
        result.txElements = {range(129, 254)};
        result.txElementsArray = {range(127, 252)};
    } else if (mode == "firstThenSecond") {
        result.txElements = {range(1, 126), range(129, 254)};
        result.txElementsArray = {range(1, 126), range(127, 252)};
    } else if (mode == "both") {
        std::vector<int> tx = range(1, 126);
        const std::vector<int> tx2 = range(129, 254);
        tx.insert(tx.end(), tx2.begin(), tx2.end());
        result.txElements = {tx};
        result.txElementsArray = {range(1, 252)};
    } else {
        throw std::invalid_argument("defineArrayTxElements: Tx Elements Not Defined");
    }
    return result;
}

std::vector<int> arrayElementsToVSXElements(const std::vector<int>& arrayElements) {
    std::vector<int> mapping = range(1, 126);
    const std::vector<int> mapping2 = range(129, 254);
    mapping.insert(mapping.end(), mapping2.begin(), mapping2.end());  // 252 entries, 1-based semantics

    std::vector<int> vsxElements;
    vsxElements.reserve(arrayElements.size());
    for (int idx1Based : arrayElements) {
        vsxElements.push_back(mapping.at(static_cast<size_t>(idx1Based - 1)));
    }
    return vsxElements;
}

}  // namespace beam::array
