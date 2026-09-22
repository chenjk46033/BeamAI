#include "correction/waveforms.hpp"

namespace beam::correction {

namespace {

// MATLAB circshift on a row: positive shift moves values toward higher
// column indices, wrapping.
Eigen::RowVectorXd circshiftRow(const Eigen::RowVectorXd& v, int shift) {
    const Eigen::Index n = v.size();
    if (n == 0) {
        return v;
    }
    Eigen::RowVectorXd out(n);
    for (Eigen::Index i = 0; i < n; ++i) {
        const Eigen::Index src = ((i - shift) % n + n) % n;
        out(i) = v(src);
    }
    return out;
}

}  // namespace

ShiftAndSumResult shiftAndSumWaveforms(const Eigen::MatrixXd& wvData, const Eigen::VectorXi& delaysI,
                                        std::optional<Eigen::VectorXd> weights) {
    const Eigen::Index nElements = wvData.rows();
    const Eigen::VectorXd w = weights.value_or(Eigen::VectorXd::Ones(nElements));

    Eigen::MatrixXd wvShifted(wvData.rows(), wvData.cols());
    for (Eigen::Index i = 0; i < nElements; ++i) {
        wvShifted.row(i) = circshiftRow(wvData.row(i), delaysI(i)) * w(i);
    }

    ShiftAndSumResult result;
    result.wvShifted = wvShifted;
    result.waveformSum = wvShifted.colwise().sum().transpose();
    return result;
}

}  // namespace beam::correction
