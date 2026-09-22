#include "correction/localize.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <functional>
#include <iterator>
#include <stdexcept>
#include <vector>

#include <Eigen/Cholesky>

#include "array/array_struct.hpp"
#include "array/array_types.hpp"
#include "correction/correlation.hpp"

namespace beam::correction {

double distance(const Eigen::Vector3d& v1, const Eigen::Vector3d& v2) {
    return (v1 - v2).norm();
}

beam::array::ArrayStruct updateArrayPositions(beam::array::ArrayStruct array,
                                               const Eigen::MatrixX3d& rs) {
    if (static_cast<size_t>(rs.rows()) != array.element.size()) {
        throw std::invalid_argument("updateArrayPositions: rs row count must match array.element size");
    }

    // rect rows (0-based) for the x/y/z of the 4 corners, per array_types.hpp.
    constexpr int xRows[4] = {1, 4, 7, 10};
    constexpr int yRows[4] = {2, 5, 8, 11};
    constexpr int zRows[4] = {3, 6, 9, 12};
    constexpr int cRow = beam::array::kRectCenterStartRow;  // 16

    for (Eigen::Index e = 0; e < rs.rows(); ++e) {
        const Eigen::Vector3d newCenter = rs.row(e).transpose();
        const Eigen::Vector3d oldCenter = array.rect.block<3, 1>(cRow, e);
        const Eigen::Vector3d shift = newCenter - oldCenter;

        array.element[static_cast<size_t>(e)].position = newCenter;
        for (int k = 0; k < 4; ++k) {
            array.rect(xRows[k], e) += shift(0);
            array.rect(yRows[k], e) += shift(1);
            array.rect(zRows[k], e) += shift(2);
        }
        array.rect.block<3, 1>(cRow, e) = newCenter;
    }
    return array;
}

Eigen::VectorXd nonlinRelativeDistanceFun(const Eigen::MatrixXi& mData, const Eigen::VectorXd& dData,
                                           int nElements, double fs, const Eigen::VectorXd& x) {
    const double c = x(0);
    // reshape(x(2:end), nElements, 3), column-major: element n's (1-based)
    // xyz is x(1 + n-1), x(1 + nElements + n-1), x(1 + 2*nElements + n-1).
    const auto pos = [&x, nElements](int n1Based) -> Eigen::Vector3d {
        const int r = n1Based - 1;
        return Eigen::Vector3d(x(1 + r), x(1 + nElements + r), x(1 + 2 * nElements + r));
    };

    Eigen::VectorXd estimated(mData.rows());
    for (Eigen::Index row = 0; row < mData.rows(); ++row) {
        const int i = mData(row, 0);
        const int j = mData(row, 1);
        const int k = mData(row, 2);
        const double disti = distance(pos(i), pos(k));
        const double distj = distance(pos(j), pos(k));
        estimated(row) = (distj - disti) / c;
    }
    return (estimated - dData).cwiseAbs() * fs;
}

namespace {

// MATLAB intersect(a, b): ascending, unique, common elements.
std::vector<int> sortedIntersection(std::vector<int> a, std::vector<int> b) {
    std::sort(a.begin(), a.end());
    a.erase(std::unique(a.begin(), a.end()), a.end());
    std::sort(b.begin(), b.end());
    b.erase(std::unique(b.begin(), b.end()), b.end());
    std::vector<int> out;
    std::set_intersection(a.begin(), a.end(), b.begin(), b.end(), std::back_inserter(out));
    return out;
}

// First min(n, v.size()) entries of v.
std::vector<int> firstN(const std::vector<int>& v, std::size_t n) {
    return std::vector<int>(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(std::min(n, v.size())));
}

}  // namespace

LocalizeSystem getLocalizeArraysSystemOfEquations(const std::vector<Eigen::MatrixXd>& wvData,
                                                   const beam::array::ArrayData& arrayData, double f,
                                                   double fs) {
    const auto& elements = arrayData.arrayTotal.element;
    const int nEl = static_cast<int>(elements.size());
    const double half = nEl / 2.0;
    constexpr std::size_t nOpposing = 20;
    const double lagWindow = 50.0 * fs / f;
    const double matchTol = (fs / f) / 6.0;
    const double d12Tol = fs / f;

    std::vector<std::array<int, 3>> mRows;
    std::vector<double> dVals;

    for (int i = 1; i <= nEl; ++i) {
        int jStart, jEnd;
        if (i < half) {
            jStart = i + 1;
            jEnd = static_cast<int>(std::floor(half));
        } else if (i == half || i == nEl) {
            continue;
        } else {
            jStart = i + 1;
            jEnd = nEl;
        }

        const std::vector<int> rcvI = firstN(elements[static_cast<std::size_t>(i - 1)].receiveElements, nOpposing);
        for (int j = jStart; j <= jEnd; ++j) {
            const std::vector<int> rcvJ =
                firstN(elements[static_cast<std::size_t>(j - 1)].receiveElements, nOpposing);
            for (int k : sortedIntersection(rcvI, rcvJ)) {
                Eigen::VectorXd s1 = wvData[static_cast<std::size_t>(i - 1)].col(k - 1);
                Eigen::VectorXd s2 = wvData[static_cast<std::size_t>(j - 1)].col(k - 1);
                s1.array() -= s1.mean();
                s2.array() -= s2.mean();

                const XcorrS1ToS2Result xc = xcorrS1ToS2(s1, s2, Eigen::Vector2d(-lagWindow, lagWindow));

                Eigen::Index mi = 0, mj = 0;
                s1.maxCoeff(&mi);
                s2.maxCoeff(&mj);
                const double maxd12 = static_cast<double>(mj - mi);

                if (std::abs(maxd12 - xc.d12) < matchTol && std::abs(static_cast<double>(xc.d12)) < d12Tol) {
                    dVals.push_back(static_cast<double>(xc.d12));
                    mRows.push_back({i, j, k});
                }
            }
        }
    }

    LocalizeSystem out;
    out.mData.resize(static_cast<Eigen::Index>(mRows.size()), 3);
    out.dData.resize(static_cast<Eigen::Index>(dVals.size()));
    for (Eigen::Index r = 0; r < out.mData.rows(); ++r) {
        out.mData(r, 0) = mRows[static_cast<std::size_t>(r)][0];
        out.mData(r, 1) = mRows[static_cast<std::size_t>(r)][1];
        out.mData(r, 2) = mRows[static_cast<std::size_t>(r)][2];
        out.dData(r) = dVals[static_cast<std::size_t>(r)] / fs;  // dData = dData/params.fs
    }
    return out;
}

namespace {

Eigen::VectorXd clampToBox(const Eigen::VectorXd& x, const Eigen::VectorXd& lb, const Eigen::VectorXd& ub) {
    return x.cwiseMax(lb).cwiseMin(ub);
}

// Forward-difference Jacobian of `residual` at `x` (m x n).
Eigen::MatrixXd numericalJacobian(const std::function<Eigen::VectorXd(const Eigen::VectorXd&)>& residual,
                                  const Eigen::VectorXd& x, const Eigen::VectorXd& f0) {
    const Eigen::Index n = x.size();
    Eigen::MatrixXd j(f0.size(), n);
    for (Eigen::Index k = 0; k < n; ++k) {
        const double h = 1e-7 * std::max(1.0, std::abs(x(k)));
        Eigen::VectorXd xh = x;
        xh(k) += h;
        j.col(k) = (residual(xh) - f0) / h;
    }
    return j;
}

// Bounded Levenberg-Marquardt: LM step, then project the iterate back into
// [lb, ub]; accept only if the residual norm improves.
Eigen::VectorXd boundedLevenbergMarquardt(
    const std::function<Eigen::VectorXd(const Eigen::VectorXd&)>& residual, Eigen::VectorXd x,
    const Eigen::VectorXd& lb, const Eigen::VectorXd& ub, int maxIterations) {
    x = clampToBox(x, lb, ub);
    double lambda = 1e-3;
    Eigen::VectorXd f = residual(x);
    double cost = f.squaredNorm();

    for (int iter = 0; iter < maxIterations; ++iter) {
        const Eigen::MatrixXd jac = numericalJacobian(residual, x, f);
        const Eigen::MatrixXd jtj = jac.transpose() * jac;
        const Eigen::VectorXd g = jac.transpose() * f;

        bool stepAccepted = false;
        for (int inner = 0; inner < 12 && !stepAccepted; ++inner) {
            const Eigen::MatrixXd a =
                jtj + lambda * Eigen::MatrixXd(jtj.diagonal().asDiagonal());
            const Eigen::VectorXd dx = a.ldlt().solve(-g);
            const Eigen::VectorXd xNew = clampToBox(x + dx, lb, ub);
            const Eigen::VectorXd fNew = residual(xNew);
            const double costNew = fNew.squaredNorm();
            if (costNew < cost) {
                x = xNew;
                f = fNew;
                cost = costNew;
                lambda = std::max(lambda * 0.5, 1e-12);
                stepAccepted = true;
            } else {
                lambda *= 3.0;
            }
        }
        if (!stepAccepted) {
            break;  // no downhill step within the box
        }
    }
    return x;
}

}  // namespace

LocalizeResult localizeArrays(const LocalizeSystem& system, const Eigen::MatrixX3d& initialPositions,
                               double c0, double fs, int maxIterations) {
    const int nElements = static_cast<int>(initialPositions.rows());

    // x = [c; positions], positions column-major (all x, then all y, then z).
    Eigen::VectorXd initial(1 + 3 * nElements);
    initial(0) = c0;
    for (int col = 0; col < 3; ++col) {
        for (int r = 0; r < nElements; ++r) {
            initial(1 + col * nElements + r) = initialPositions(r, col);
        }
    }

    Eigen::VectorXd lb = initial;
    Eigen::VectorXd ub = initial;
    lb(0) = c0 - 20.0;
    ub(0) = c0 + 20.0;

    // MATLAB: topRow = 1:9:end, bottomRow = 9:9:end (1-based element
    // numbers); those elements stay pinned, every other element moves +/-2.3mm.
    constexpr double kSlack = 0.0023;
    for (int e = 0; e < nElements; ++e) {
        const int oneBased = e + 1;
        const bool pinned = (oneBased % 9 == 1) || (oneBased % 9 == 0);
        if (pinned) {
            continue;  // lb == ub == initial for this element's x/y/z
        }
        for (int col = 0; col < 3; ++col) {
            const Eigen::Index idx = 1 + col * nElements + e;
            lb(idx) = initial(idx) - kSlack;
            ub(idx) = initial(idx) + kSlack;
        }
    }

    const Eigen::MatrixXi mData = system.mData;
    const Eigen::VectorXd dData = system.dData;
    const auto residual = [&](const Eigen::VectorXd& x) {
        return nonlinRelativeDistanceFun(mData, dData, nElements, fs, x);
    };

    const Eigen::VectorXd solution = boundedLevenbergMarquardt(residual, initial, lb, ub, maxIterations);

    LocalizeResult result;
    result.c = solution(0);
    result.positions.resize(nElements, 3);
    for (int col = 0; col < 3; ++col) {
        for (int r = 0; r < nElements; ++r) {
            result.positions(r, col) = solution(1 + col * nElements + r);
        }
    }
    return result;
}

}  // namespace beam::correction
