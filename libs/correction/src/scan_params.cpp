#include "correction/scan_params.hpp"

namespace beam::correction {

TxRxScanParams defineTxRxScanParams() {
    TxRxScanParams p;
    p.arrayToVSXMapping.reserve(252);
    for (int i = 1; i <= 126; ++i) {
        p.arrayToVSXMapping.push_back(i);
    }
    for (int i = 129; i <= 254; ++i) {
        p.arrayToVSXMapping.push_back(i);
    }
    return p;
}

}  // namespace beam::correction
