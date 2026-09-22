#include "array/array_data.hpp"

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
