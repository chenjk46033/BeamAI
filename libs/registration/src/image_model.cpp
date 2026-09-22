#include "registration/image_model.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <map>
#include <numeric>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace beam::registration {

double orientationAngleFromExif(int orientationTag) {
    switch (orientationTag) {
        case 1: return 0.0;
        case 3: return 180.0;
        case 6: return 90.0;
        case 8: return -90.0;
        default:
            throw std::invalid_argument("orientationAngleFromExif: non-standard EXIF Orientation tag " +
                                        std::to_string(orientationTag));
    }
}

namespace {

using Point = std::pair<int, int>;  // (row, col)

// Row-index of the first perimline row equal to (r, c), or -1.
std::map<Point, int> buildRowIndex(const Eigen::MatrixX2i& perimline) {
    std::map<Point, int> index;
    for (Eigen::Index i = 0; i < perimline.rows(); ++i) {
        const Point key{perimline(i, 0), perimline(i, 1)};
        index.emplace(key, static_cast<int>(i));  // keeps the first
    }
    return index;
}

}  // namespace

Eigen::MatrixX2i perimwalkImage2(const Eigen::MatrixX2i& perimline, const Eigen::Vector2i& startpoint) {
    const std::map<Point, int> rowIndex = buildRowIndex(perimline);
    const auto startIt = rowIndex.find({startpoint(0), startpoint(1)});
    if (startIt == rowIndex.end()) {
        throw std::invalid_argument("perimwalkImage2: start point is not in the perimeter list");
    }

    const std::array<Point, 8> offsets = {{{1, -1}, {1, 0}, {1, 1}, {0, 1}, {0, -1}, {-1, 1}, {-1, -1}, {-1, 0}}};

    std::vector<char> visited(static_cast<std::size_t>(perimline.rows()), 0);
    std::vector<int> stack{startIt->second};
    std::vector<Point> sorted;

    while (!stack.empty()) {
        const int current = stack.back();
        stack.pop_back();
        if (visited[static_cast<std::size_t>(current)]) {
            continue;
        }
        visited[static_cast<std::size_t>(current)] = 1;
        const Point cp{perimline(current, 0), perimline(current, 1)};
        sorted.push_back(cp);

        for (const auto& off : offsets) {
            const Point np{cp.first + off.first, cp.second + off.second};
            const auto it = rowIndex.find(np);
            if (it != rowIndex.end() && !visited[static_cast<std::size_t>(it->second)]) {
                stack.push_back(it->second);
            }
        }
    }

    // Split `sorted` at coordinate jumps > 10 (either axis).
    const int m = static_cast<int>(sorted.size());
    std::vector<int> jumpIndices{0};  // MATLAB [0; find(jumps)], 1-based positions
    for (int j = 0; j + 1 < m; ++j) {
        if (std::abs(sorted[j + 1].first - sorted[j].first) > 10 ||
            std::abs(sorted[j + 1].second - sorted[j].second) > 10) {
            jumpIndices.push_back(j + 1);  // 1-based index into the length m-1 diff
        }
    }

    // Score each segment; pick the first with the max score.
    int bestStart = 0, bestEnd = m;  // [bestStart, bestEnd) 0-based
    long bestScore = -1;
    for (std::size_t k = 0; k < jumpIndices.size(); ++k) {
        const int segStart = jumpIndices[k];  // 0-based row = MATLAB jump_indices(k)+1 - 1
        const int segEnd = (k + 1 < jumpIndices.size()) ? jumpIndices[k + 1] : m;  // exclusive
        const int size = segEnd - segStart;

        long score = 0;
        if (size > 50) {
            const int r50 = sorted[static_cast<std::size_t>(segStart) + 49].first;   // MATLAB (50,1)
            const int r1 = sorted[static_cast<std::size_t>(segStart)].first;         // MATLAB (1,1)
            score = (r50 > r1) ? 0 : size;
        }
        if (score > bestScore) {
            bestScore = score;
            bestStart = segStart;
            bestEnd = segEnd;
        }
    }

    Eigen::MatrixX2i out(bestEnd - bestStart, 2);
    for (int i = bestStart; i < bestEnd; ++i) {
        out(i - bestStart, 0) = sorted[static_cast<std::size_t>(i)].first;
        out(i - bestStart, 1) = sorted[static_cast<std::size_t>(i)].second;
    }
    return out;
}

Eigen::VectorXd perimdistanceImage(const Eigen::MatrixX2i& perimlineSorted, double hRatio, double wRatio) {
    const Eigen::Index n = perimlineSorted.rows();
    if (n <= 1) {
        return Eigen::VectorXd();
    }
    Eigen::VectorXd dist(n - 1);
    for (Eigen::Index j = 0; j + 1 < n; ++j) {
        const double dr = (perimlineSorted(j + 1, 0) - perimlineSorted(j, 0)) * hRatio;
        const double dc = (perimlineSorted(j + 1, 1) - perimlineSorted(j, 1)) * wRatio;
        dist(j) = std::sqrt(dr * dr + dc * dc);
    }
    return dist;
}

Eigen::MatrixX2i getDiscreteFromImage(const Eigen::MatrixXi& contour, double spacings,
                                       const Eigen::Vector2i& nzImg, const Eigen::Vector2i& izImg) {
    // MATLAB: NzImg = [NzImg(2), NzImg(1)] + 1  (input [x, y] -> [row, col] 1-based).
    Eigen::Vector2i nzRc(nzImg(1) + 1, nzImg(0) + 1);
    const Eigen::Vector2i izRc(izImg(1) + 1, izImg(0) + 1);

    // [r, c] = find(contour), column-major, 1-based.
    std::vector<int> rs, cs;
    for (Eigen::Index c = 0; c < contour.cols(); ++c) {
        for (Eigen::Index r = 0; r < contour.rows(); ++r) {
            if (contour(r, c) != 0) {
                rs.push_back(static_cast<int>(r) + 1);
                cs.push_back(static_cast<int>(c) + 1);
            }
        }
    }
    if (rs.empty()) {
        return Eigen::MatrixX2i(0, 2);
    }

    const int cropR = *std::max_element(rs.begin(), rs.end()) - 20;
    std::vector<Point> cntcat;
    for (std::size_t i = 0; i < rs.size(); ++i) {
        if (rs[i] <= cropR) {
            cntcat.emplace_back(rs[i], cs[i]);
        }
    }
    if (cntcat.empty()) {
        return Eigen::MatrixX2i(0, 2);
    }

    Eigen::MatrixX2i cnt(static_cast<Eigen::Index>(cntcat.size()), 2);
    for (std::size_t i = 0; i < cntcat.size(); ++i) {
        cnt(static_cast<Eigen::Index>(i), 0) = cntcat[i].first;
        cnt(static_cast<Eigen::Index>(i), 1) = cntcat[i].second;
    }

    const auto argminDistTo = [](const Eigen::MatrixX2i& pts, const Eigen::Vector2i& target) {
        Eigen::Index best = 0;
        double bestD = std::numeric_limits<double>::infinity();
        for (Eigen::Index i = 0; i < pts.rows(); ++i) {
            const double dr = pts(i, 0) - target(0);
            const double dc = pts(i, 1) - target(1);
            const double d = dr * dr + dc * dc;
            if (d < bestD) {
                bestD = d;
                best = i;
            }
        }
        return best;
    };

    const Eigen::Index nzIdx = argminDistTo(cnt, nzRc);
    nzRc = cnt.row(nzIdx).transpose();

    const Eigen::MatrixX2i perimline = perimwalkImage2(cnt, nzRc);
    const Eigen::VectorXd perimdist = perimdistanceImage(perimline, spacings, spacings);

    const Eigen::Index izCntIdx = argminDistTo(perimline, izRc);

    // sumdist = cumsum(perimdist), length perimline.rows() - 1.
    Eigen::VectorXd sumdist(perimdist.size());
    double acc = 0.0;
    for (Eigen::Index i = 0; i < perimdist.size(); ++i) {
        acc += perimdist(i);
        sumdist(i) = acc;
    }

    // MATLAB: if IzCntIdx (1-based) <= length(sumdist)  ->  izCntIdx (0-based) <= sumdist.size()-1.
    if (izCntIdx > sumdist.size() - 1) {
        return Eigen::MatrixX2i(0, 2);
    }

    static constexpr double kPercentiles[7] = {0.0, 0.1, 0.3, 0.5, 0.7, 0.9, 1.0};
    const double izDist = sumdist(izCntIdx);

    Eigen::MatrixX2i markers(7, 2);
    for (int k = 0; k < 7; ++k) {
        const double target = kPercentiles[k] * izDist;
        Eigen::Index best = 0;
        double bestD = std::numeric_limits<double>::infinity();
        for (Eigen::Index i = 0; i < sumdist.size(); ++i) {
            const double d = std::abs(sumdist(i) - target);
            if (d < bestD) {
                bestD = d;
                best = i;
            }
        }
        markers.row(k) = perimline.row(best);
    }
    return markers;
}

std::vector<FiducialMarker> organizeTransducerSlots(
    const Eigen::MatrixX3d& transducerSlotsRasMm, const std::vector<FiducialMarker>& arrayFiducialMarkers) {
    // YZ columns: Y = col 1, Z = col 2 (0-based) of the RAS mm slots.
    const Eigen::Index n = transducerSlotsRasMm.rows();
    std::vector<int> byZ(static_cast<std::size_t>(n));
    std::iota(byZ.begin(), byZ.end(), 0);
    std::stable_sort(byZ.begin(), byZ.end(), [&](int a, int b) {
        return transducerSlotsRasMm(a, 2) < transducerSlotsRasMm(b, 2);
    });

    // bottomRow = 4 lowest-Z; sort those by Y descending.
    std::vector<int> bottom(byZ.begin(), byZ.begin() + 4);
    std::stable_sort(bottom.begin(), bottom.end(), [&](int a, int b) {
        return transducerSlotsRasMm(a, 1) > transducerSlotsRasMm(b, 1);
    });

    std::vector<int> byYDesc(static_cast<std::size_t>(n));
    std::iota(byYDesc.begin(), byYDesc.end(), 0);
    std::stable_sort(byYDesc.begin(), byYDesc.end(), [&](int a, int b) {
        return transducerSlotsRasMm(a, 1) > transducerSlotsRasMm(b, 1);
    });

    // leftRow = 4 highest-Y; sort those by Z ascending.
    std::vector<int> left(byYDesc.begin(), byYDesc.begin() + 4);
    std::stable_sort(left.begin(), left.end(), [&](int a, int b) {
        return transducerSlotsRasMm(a, 2) < transducerSlotsRasMm(b, 2);
    });

    const auto yz = [&](int slot) {
        return Eigen::Vector2d(transducerSlotsRasMm(slot, 1), transducerSlotsRasMm(slot, 2));
    };
    const auto xMm = [&](int marker) { return arrayFiducialMarkers[static_cast<std::size_t>(marker)].position.x() * 1000.0; };

    std::vector<FiducialMarker> out(6);
    const Eigen::Vector2d y1z4 = yz(left[3]);        // leftRowSorted(4,:)
    const Eigen::Vector2d y1z1 = yz(bottom[0]);      // bottomRowSorted(1,:)
    const Eigen::Vector2d y4z1 = yz(bottom[3]);      // bottomRowSorted(end,:)

    out[0].name = arrayFiducialMarkers[0].name;
    out[0].position = Eigen::Vector3d(xMm(0), y1z4(0), y1z4(1));
    out[1].name = arrayFiducialMarkers[1].name;
    out[1].position = Eigen::Vector3d(xMm(1), y1z1(0), y1z1(1));
    out[2].name = arrayFiducialMarkers[2].name;
    out[2].position = Eigen::Vector3d(xMm(2), y4z1(0), y4z1(1));
    for (int i = 3; i < 6; ++i) {
        out[static_cast<std::size_t>(i)].name = arrayFiducialMarkers[static_cast<std::size_t>(i)].name;
        const Eigen::Vector3d& prev = out[static_cast<std::size_t>(i - 3)].position;
        out[static_cast<std::size_t>(i)].position = Eigen::Vector3d(xMm(i), prev(1), prev(2));
    }
    return out;
}

}  // namespace beam::registration
