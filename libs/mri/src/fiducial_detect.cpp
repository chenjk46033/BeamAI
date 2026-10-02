#include "mri/fiducial_detect.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace beam::mri {
namespace {

struct Annulus {
    double radiusMm = 0.0;
    int half = 0;
    Eigen::MatrixXd weights;  // zero-mean, unit-norm
};

std::vector<Annulus> buildAnnulusBank(const FiducialDetectOptions& options, double apStep,
                                      double isStep) {
    constexpr double kRingHalfWidthMm = 0.8;
    constexpr double kCoreMarginMm = 1.6;
    std::vector<Annulus> bank;
    for (double radius = options.minRadiusMm; radius <= options.maxRadiusMm + 1e-9;
         radius += options.radiusStepMm) {
        Annulus annulus;
        annulus.radiusMm = radius;
        annulus.half = static_cast<int>(std::ceil((radius + 2.5) / std::min(apStep, isStep)));
        const int size = 2 * annulus.half + 1;
        annulus.weights.resize(size, size);
        for (int a = -annulus.half; a <= annulus.half; ++a) {
            for (int b = -annulus.half; b <= annulus.half; ++b) {
                const double distance = std::hypot(a * apStep, b * isStep);
                double weight = 0.0;
                if (std::abs(distance - radius) <= kRingHalfWidthMm) {
                    weight = 1.0;
                } else if (distance < radius - kCoreMarginMm || distance > radius + kCoreMarginMm) {
                    weight = -1.0;
                }
                annulus.weights(a + annulus.half, b + annulus.half) = weight;
            }
        }
        annulus.weights.array() -= annulus.weights.mean();
        const double norm = annulus.weights.norm();
        if (norm > 0.0) annulus.weights /= norm;
        bank.push_back(std::move(annulus));
    }
    return bank;
}

double axisStep(const Eigen::VectorXd& axis) {
    return axis.size() > 1 ? std::abs(axis(1) - axis(0)) : 1.0;
}

double signedAxisStep(const Eigen::VectorXd& axis) {
    return axis.size() > 1 ? axis(1) - axis(0) : 0.0;
}

Eigen::Index nearestIndex(const Eigen::VectorXd& axis, double mm) {
    Eigen::Index best = 0;
    (axis.array() - mm).abs().minCoeff(&best);
    return best;
}

// Vertex of the parabola through (-1,a) (0,b) (1,c). Turns a peak quantised
// to the voxel grid into a sub-voxel one.
double parabolaOffset(double a, double b, double c) {
    const double denominator = a - 2.0 * b + c;
    if (std::abs(denominator) < 1e-12) return 0.0;
    return std::clamp(0.5 * (a - c) / denominator, -0.5, 0.5);
}

struct Peak {
    Eigen::Index i = 0;
    Eigen::Index j = 0;
    Eigen::Index k = 0;
    double response = 0.0;
    double radiusMm = 0.0;
    double stability = 0.0;
    Eigen::Vector3d positionMm = Eigen::Vector3d::Zero();
};

// One panel's correlation maps, one per sagittal slice, sharing a window.
struct SideMaps {
    Eigen::Index iLo = 0;
    Eigen::Index jLo = 0;
    Eigen::Index kLo = 0;
    std::vector<Eigen::MatrixXd> response;  // response[s](j - jLo, k - kLo)
    std::vector<Eigen::MatrixXd> radius;
};

SideMaps correlateSide(const Volume3D& volume, const std::vector<Annulus>& bank, Eigen::Index iLo,
                       Eigen::Index iHi, Eigen::Index jLo, Eigen::Index jHi, Eigen::Index kLo,
                       Eigen::Index kHi) {
    SideMaps maps;
    maps.iLo = iLo;
    maps.jLo = jLo;
    maps.kLo = kLo;
    const Eigen::Index rows = jHi - jLo + 1;
    const Eigen::Index cols = kHi - kLo + 1;

    for (Eigen::Index i = iLo; i <= iHi; ++i) {
        Eigen::MatrixXd best = Eigen::MatrixXd::Constant(rows, cols, -1.0);
        Eigen::MatrixXd bestRadius = Eigen::MatrixXd::Zero(rows, cols);
        for (const Annulus& annulus : bank) {
            const int half = annulus.half;
            const double count = static_cast<double>((2 * half + 1) * (2 * half + 1));
            for (Eigen::Index j = jLo; j <= jHi; ++j) {
                for (Eigen::Index k = kLo; k <= kHi; ++k) {
                    // Reads outside the requested window but inside the
                    // volume, so every centre in the box gets a real
                    // correlation instead of a half-kernel dead border.
                    if (j - half < 0 || j + half >= volume.ny) continue;
                    if (k - half < 0 || k + half >= volume.nz) continue;
                    double dot = 0.0;
                    double sum = 0.0;
                    double sumSquares = 0.0;
                    for (int a = -half; a <= half; ++a) {
                        for (int b = -half; b <= half; ++b) {
                            const double value = volume(i, j + a, k + b);
                            dot += value * annulus.weights(a + half, b + half);
                            sum += value;
                            sumSquares += value * value;
                        }
                    }
                    const double variance = sumSquares - sum * sum / count;
                    if (variance <= 1e-12) continue;
                    const double ncc = dot / std::sqrt(variance);
                    if (ncc > best(j - jLo, k - kLo)) {
                        best(j - jLo, k - kLo) = ncc;
                        bestRadius(j - jLo, k - kLo) = annulus.radiusMm;
                    }
                }
            }
        }
        maps.response.push_back(std::move(best));
        maps.radius.push_back(std::move(bestRadius));
    }
    return maps;
}

std::vector<Peak> localMaxima(const SideMaps& maps, const FiducialDetectOptions& options) {
    std::vector<Peak> peaks;
    for (std::size_t s = 0; s < maps.response.size(); ++s) {
        const Eigen::MatrixXd& map = maps.response[s];
        for (Eigen::Index r = 1; r + 1 < map.rows(); ++r) {
            for (Eigen::Index c = 1; c + 1 < map.cols(); ++c) {
                const double value = map(r, c);
                if (value < options.responseFloor) continue;
                bool isMaximum = true;
                for (int dr = -1; dr <= 1 && isMaximum; ++dr) {
                    for (int dc = -1; dc <= 1; ++dc) {
                        if (dr == 0 && dc == 0) continue;
                        if (map(r + dr, c + dc) > value) {
                            isMaximum = false;
                            break;
                        }
                    }
                }
                if (!isMaximum) continue;
                Peak peak;
                peak.i = maps.iLo + static_cast<Eigen::Index>(s);
                peak.j = maps.jLo + r;
                peak.k = maps.kLo + c;
                peak.response = value;
                peak.radiusMm = maps.radius[s](r, c);
                peaks.push_back(peak);
            }
        }
    }
    return peaks;
}

Eigen::Vector3d refine(const SideMaps& maps, const RasAxisVectors& axes, const Peak& peak) {
    const std::size_t s = static_cast<std::size_t>(peak.i - maps.iLo);
    const Eigen::Index r = peak.j - maps.jLo;
    const Eigen::Index c = peak.k - maps.kLo;
    const Eigen::MatrixXd& map = maps.response[s];

    double lrOffset = 0.0;
    if (s > 0 && s + 1 < maps.response.size()) {
        lrOffset = parabolaOffset(maps.response[s - 1](r, c), peak.response,
                                  maps.response[s + 1](r, c));
    }
    const double apOffset = parabolaOffset(map(r - 1, c), peak.response, map(r + 1, c));
    const double isOffset = parabolaOffset(map(r, c - 1), peak.response, map(r, c + 1));

    return {axes.dimLR(peak.i) + lrOffset * signedAxisStep(axes.dimLR),
            axes.dimAP(peak.j) + apOffset * signedAxisStep(axes.dimAP),
            axes.dimIS(peak.k) + isOffset * signedAxisStep(axes.dimIS)};
}

// How well neighbouring slices agree on the in-plane centre. A donut is a
// solid of revolution: its centre barely moves across the slices that cut
// it, while a noise peak wanders. This discriminates better than peak height.
double stabilityOf(const SideMaps& maps, const RasAxisVectors& axes, const Peak& peak,
                   double searchRadiusMm, double toleranceMm) {
    const std::size_t centre = static_cast<std::size_t>(peak.i - maps.iLo);
    double total = 0.0;
    int counted = 0;
    for (int offset : {-2, -1, 1, 2}) {
        const long long index = static_cast<long long>(centre) + offset;
        if (index < 0 || index >= static_cast<long long>(maps.response.size())) continue;
        const Eigen::MatrixXd& map = maps.response[static_cast<std::size_t>(index)];
        double best = -1.0;
        double bestDistance = 0.0;
        for (Eigen::Index r = 0; r < map.rows(); ++r) {
            for (Eigen::Index c = 0; c < map.cols(); ++c) {
                const double distance = std::hypot(axes.dimAP(maps.jLo + r) - axes.dimAP(peak.j),
                                                   axes.dimIS(maps.kLo + c) - axes.dimIS(peak.k));
                if (distance > searchRadiusMm) continue;
                if (map(r, c) > best) {
                    best = map(r, c);
                    bestDistance = distance;
                }
            }
        }
        if (best < 0.0) continue;
        total += std::clamp(1.0 - bestDistance / toleranceMm, 0.0, 1.0);
        ++counted;
    }
    return counted > 0 ? total / counted : 0.0;
}

// Does `actual` run the same way as `expected` along AP and IS? Only
// components the prior makes meaningful are checked, so the panel's
// hypotenuse edge -- whose AP and IS legs are within 2.5mm of each other --
// cannot be rejected over which of the two happens to be larger.
bool edgeOrientationAgrees(const Eigen::Vector3d& expected, const Eigen::Vector3d& actual) {
    constexpr double kMeaningfulMm = 5.0;
    for (int axis : {1, 2}) {
        if (std::abs(expected(axis)) < kMeaningfulMm) continue;
        if (expected(axis) * actual(axis) <= 0.0) return false;
    }
    return true;
}

}  // namespace

std::vector<FiducialDetection> detectFiducialDonuts(const Volume3D& volume,
                                                    const RasAxisVectors& axes,
                                                    const std::vector<Eigen::Vector3d>& priorMm,
                                                    const std::vector<std::string>& names,
                                                    const FiducialDetectOptions& options) {
    if (priorMm.size() != 6) {
        throw std::invalid_argument("detectFiducialDonuts needs 6 prior positions");
    }
    if (names.size() != 6) {
        throw std::invalid_argument("detectFiducialDonuts needs 6 marker names");
    }
    if (axes.dimLR.size() != volume.nx || axes.dimAP.size() != volume.ny ||
        axes.dimIS.size() != volume.nz) {
        throw std::invalid_argument("detectFiducialDonuts: volume and axes disagree on dimensions");
    }

    const double apStep = axisStep(axes.dimAP);
    const double isStep = axisStep(axes.dimIS);
    const std::vector<Annulus> bank = buildAnnulusBank(options, apStep, isStep);

    std::vector<FiducialDetection> results(6);
    for (std::size_t index = 0; index < 6; ++index) results[index].name = names[index];

    // Split the prior into its two panels on the midline rather than trusting
    // a fixed index order.
    std::vector<double> lateral;
    for (const Eigen::Vector3d& position : priorMm) lateral.push_back(position.x());
    std::vector<double> sorted = lateral;
    std::sort(sorted.begin(), sorted.end());
    const double midline = 0.5 * (sorted[2] + sorted[3]);

    for (int side = 0; side < 2; ++side) {
        std::vector<int> group;
        for (int index = 0; index < 6; ++index) {
            if ((lateral[static_cast<std::size_t>(index)] < midline) == (side == 0)) {
                group.push_back(index);
            }
        }
        if (group.size() != 3) continue;

        Eigen::Vector3d low = priorMm[static_cast<std::size_t>(group[0])];
        Eigen::Vector3d high = low;
        for (int index : group) {
            low = low.cwiseMin(priorMm[static_cast<std::size_t>(index)]);
            high = high.cwiseMax(priorMm[static_cast<std::size_t>(index)]);
        }

        const auto range = [](const Eigen::VectorXd& axis, double lo, double hi, Eigen::Index n) {
            Eigen::Index a = nearestIndex(axis, lo);
            Eigen::Index b = nearestIndex(axis, hi);
            if (a > b) std::swap(a, b);
            return std::pair<Eigen::Index, Eigen::Index>{std::clamp<Eigen::Index>(a, 0, n - 1),
                                                         std::clamp<Eigen::Index>(b, 0, n - 1)};
        };
        const double lrCentre = 0.5 * (low.x() + high.x());
        const auto lrRange = range(axes.dimLR, lrCentre - options.lrMarginMm,
                                   lrCentre + options.lrMarginMm, volume.nx);
        const auto apRange = range(axes.dimAP, low.y() - options.apMarginMm,
                                   high.y() + options.apMarginMm, volume.ny);
        const auto isRange = range(axes.dimIS, low.z() - options.isMarginInferiorMm,
                                   high.z() + options.isMarginSuperiorMm, volume.nz);

        const SideMaps maps = correlateSide(volume, bank, lrRange.first, lrRange.second,
                                            apRange.first, apRange.second, isRange.first,
                                            isRange.second);
        std::vector<Peak> peaks = localMaxima(maps, options);
        for (Peak& peak : peaks) peak.positionMm = refine(maps, axes, peak);
        std::sort(peaks.begin(), peaks.end(),
                  [](const Peak& a, const Peak& b) { return a.response > b.response; });

        // Collapse the responses one donut produces on neighbouring slices.
        std::vector<Peak> candidates;
        for (const Peak& peak : peaks) {
            const bool duplicate =
                std::any_of(candidates.begin(), candidates.end(), [&](const Peak& kept) {
                    return (kept.positionMm - peak.positionMm).norm() < options.clusterRadiusMm;
                });
            if (!duplicate) candidates.push_back(peak);
            if (static_cast<int>(candidates.size()) >= options.candidatesPerSide) break;
        }
        for (Peak& candidate : candidates) {
            candidate.stability = stabilityOf(maps, axes, candidate, options.clusterRadiusMm,
                                              3.0 * std::max(apStep, isStep));
        }

        // Assign candidates to the panel's three markers using the prior's
        // triangle: pairwise lengths plus the sign of each edge on its
        // dominant axis, so the three cannot be permuted.
        const Eigen::Vector3d p0 = priorMm[static_cast<std::size_t>(group[0])];
        const Eigen::Vector3d p1 = priorMm[static_cast<std::size_t>(group[1])];
        const Eigen::Vector3d p2 = priorMm[static_cast<std::size_t>(group[2])];
        const std::array<double, 3> expected{(p0 - p1).norm(), (p0 - p2).norm(), (p1 - p2).norm()};
        const std::array<Eigen::Vector3d, 3> expectedEdges{p0 - p1, p0 - p2, p1 - p2};
        // The measured panel triangle and the nominal one disagree by ~3.5mm
        // on the AP arm (docs/known_gaps_mri.md), so this must not encode the
        // nominal lengths tightly.
        constexpr double kEdgeToleranceMm = 8.0;

        double bestScore = -1.0;
        std::array<int, 3> bestTriple{-1, -1, -1};
        const int count = static_cast<int>(candidates.size());
        for (int a = 0; a < count; ++a) {
            for (int b = 0; b < count; ++b) {
                if (b == a) continue;
                for (int c = 0; c < count; ++c) {
                    if (c == a || c == b) continue;
                    const Eigen::Vector3d& qa = candidates[static_cast<std::size_t>(a)].positionMm;
                    const Eigen::Vector3d& qb = candidates[static_cast<std::size_t>(b)].positionMm;
                    const Eigen::Vector3d& qc = candidates[static_cast<std::size_t>(c)].positionMm;
                    const std::array<double, 3> actual{(qa - qb).norm(), (qa - qc).norm(),
                                                       (qb - qc).norm()};
                    bool consistent = true;
                    for (int edge = 0; edge < 3 && consistent; ++edge) {
                        consistent = std::abs(actual[static_cast<std::size_t>(edge)] -
                                              expected[static_cast<std::size_t>(edge)]) <=
                                     kEdgeToleranceMm;
                    }
                    if (!consistent) continue;
                    const std::array<Eigen::Vector3d, 3> actualEdges{qa - qb, qa - qc, qb - qc};
                    for (int edge = 0; edge < 3 && consistent; ++edge) {
                        consistent = edgeOrientationAgrees(expectedEdges[static_cast<std::size_t>(edge)],
                                                           actualEdges[static_cast<std::size_t>(edge)]);
                    }
                    if (!consistent) continue;
                    const double score = candidates[static_cast<std::size_t>(a)].response +
                                         candidates[static_cast<std::size_t>(b)].response +
                                         candidates[static_cast<std::size_t>(c)].response;
                    if (score > bestScore) {
                        bestScore = score;
                        bestTriple = {a, b, c};
                    }
                }
            }
        }

        for (int slot = 0; slot < 3; ++slot) {
            const int chosen = bestTriple[static_cast<std::size_t>(slot)];
            if (chosen < 0) continue;
            const Peak& peak = candidates[static_cast<std::size_t>(chosen)];
            FiducialDetection& out = results[static_cast<std::size_t>(group[slot])];
            out.positionMm = peak.positionMm;
            out.response = peak.response;
            out.stability = peak.stability;
            out.confidence = peak.response;
            out.radiusMm = peak.radiusMm;
            out.found = true;
        }
    }
    return results;
}

}  // namespace beam::mri
