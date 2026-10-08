#include "mri/fiducial_detect.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <thread>
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
        // Both lines are load-bearing further down, and in this order --
        // centring changes the norm, scaling does not change a zero sum.
        //   sum of weights == 0  lets correlateSide feed raw voxels in: the
        //                        window's mean multiplies this sum and so
        //                        cancels, which is why nothing centres the
        //                        window at any of the tens of thousands of
        //                        positions it is evaluated at.
        //   sum of squares == 1  puts all six radii on one scale, so the
        //                        per-position max across the bank compares
        //                        like with like.
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

// Sub-voxel peak: vertex of the parabola through (-1,a) (0,b) (1,c).
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

// One panel, one map per sagittal slice over a shared window.
struct SideMaps {
    Eigen::Index iLo = 0;
    Eigen::Index jLo = 0;
    Eigen::Index kLo = 0;
    std::vector<Eigen::MatrixXd> response;  // response[s](j - jLo, k - kLo)
    std::vector<Eigen::MatrixXd> radius;
};

SideMaps correlateSide(const Volume3D& volume, const std::vector<Annulus>& bank, Eigen::Index iLo,
                       Eigen::Index iHi, Eigen::Index jLo, Eigen::Index jHi, Eigen::Index kLo,
                       Eigen::Index kHi, unsigned maxThreads) {
    SideMaps maps;
    maps.iLo = iLo;
    maps.jLo = jLo;
    maps.kLo = kLo;
    const Eigen::Index rows = jHi - jLo + 1;
    const Eigen::Index cols = kHi - kLo + 1;

    int maxHalf = 0;
    for (const Annulus& annulus : bank) maxHalf = std::max(maxHalf, annulus.half);

    // Volume3D holds one matrix per axial slice, so a sagittal window reads one
    // element from each of (2*half+1) different matrices. Copy the plane out
    // once per slice and the inner loops walk contiguous memory.
    const Eigen::Index jPadLo = std::max<Eigen::Index>(0, jLo - maxHalf);
    const Eigen::Index jPadHi = std::min<Eigen::Index>(volume.ny - 1, jHi + maxHalf);
    const Eigen::Index kPadLo = std::max<Eigen::Index>(0, kLo - maxHalf);
    const Eigen::Index kPadHi = std::min<Eigen::Index>(volume.nz - 1, kHi + maxHalf);
    const Eigen::Index planeRows = jPadHi - jPadLo + 1;
    const Eigen::Index planeCols = kPadHi - kPadLo + 1;

    const std::size_t sliceCount = static_cast<std::size_t>(iHi - iLo + 1);
    maps.response.resize(sliceCount);
    maps.radius.resize(sliceCount);

    // Slices are independent, so the result does not depend on how they are
    // divided up.
    const auto correlateOneSlice = [&](Eigen::Index i) {
        Eigen::MatrixXd plane(planeRows, planeCols);
        Eigen::MatrixXd integral = Eigen::MatrixXd::Zero(planeRows + 1, planeCols + 1);
        Eigen::MatrixXd integralSquares = Eigen::MatrixXd::Zero(planeRows + 1, planeCols + 1);
        for (Eigen::Index k = kPadLo; k <= kPadHi; ++k)
            for (Eigen::Index j = jPadLo; j <= jPadHi; ++j)
                plane(j - jPadLo, k - kPadLo) = volume(i, j, k);

        // Window sum and sum of squares are radius-independent; computing them
        // per radius was two thirds of the work.
        for (Eigen::Index c = 0; c < planeCols; ++c) {
            for (Eigen::Index r = 0; r < planeRows; ++r) {
                const double value = plane(r, c);
                integral(r + 1, c + 1) =
                    value + integral(r, c + 1) + integral(r + 1, c) - integral(r, c);
                integralSquares(r + 1, c + 1) = value * value + integralSquares(r, c + 1) +
                                                integralSquares(r + 1, c) - integralSquares(r, c);
            }
        }

        Eigen::MatrixXd best = Eigen::MatrixXd::Constant(rows, cols, -1.0);
        Eigen::MatrixXd bestRadius = Eigen::MatrixXd::Zero(rows, cols);
        for (const Annulus& annulus : bank) {
            const int half = annulus.half;
            const Eigen::Index span = 2 * half + 1;
            const double count = static_cast<double>(span * span);
            for (Eigen::Index j = jLo; j <= jHi; ++j) {
                // Reads outside the window but inside the volume: avoids a
                // half-kernel dead border at the box edge.
                if (j - half < 0 || j + half >= volume.ny) continue;
                const Eigen::Index r0 = j - half - jPadLo;
                for (Eigen::Index k = kLo; k <= kHi; ++k) {
                    if (k - half < 0 || k + half >= volume.nz) continue;
                    const Eigen::Index c0 = k - half - kPadLo;
                    const double sum = integral(r0 + span, c0 + span) - integral(r0, c0 + span) -
                                       integral(r0 + span, c0) + integral(r0, c0);
                    const double sumSquares =
                        integralSquares(r0 + span, c0 + span) - integralSquares(r0, c0 + span) -
                        integralSquares(r0 + span, c0) + integralSquares(r0, c0);
                    // Sum of squared deviations, not a variance -- no /count.
                    // It is the squared norm of the window after its mean is
                    // removed, which is the only thing the denominator needs.
                    const double variance = sumSquares - sum * sum / count;
                    // A flat window has no norm to divide by. Skipping says
                    // "no answer" rather than emitting a 0 that would then
                    // compete in localMaxima.
                    if (variance <= 1e-12) continue;
                    double dot = 0.0;
                    for (Eigen::Index b = 0; b < span; ++b) {
                        const double* pixels = &plane(r0, c0 + b);
                        const double* weights = &annulus.weights(0, b);
                        for (Eigen::Index a = 0; a < span; ++a) dot += pixels[a] * weights[a];
                    }
                    // The full zero-mean normalised cross-correlation, in
                    // [-1, 1], despite `pixels` being raw: the template's zero
                    // sum removed the window's mean from the numerator, and
                    // sqrt(variance) is the window's own norm. Invariant to
                    // window brightness and contrast, so a faint marker scores
                    // the same as a bright one.
                    const double ncc = dot / std::sqrt(variance);
                    if (ncc > best(j - jLo, k - kLo)) {
                        best(j - jLo, k - kLo) = ncc;
                        bestRadius(j - jLo, k - kLo) = annulus.radiusMm;
                    }
                }
            }
        }
        const std::size_t slot = static_cast<std::size_t>(i - iLo);
        maps.response[slot] = std::move(best);
        maps.radius[slot] = std::move(bestRadius);
    };

    const unsigned hardware =
        maxThreads > 0 ? maxThreads : std::max(1u, std::thread::hardware_concurrency());
    // At least two slices each. One slice per thread leaves most of them idle
    // after a single slice while the whole panel waits on whichever landed on
    // a slow core -- measurably worse than using fewer threads.
    const std::size_t workers =
        std::min<std::size_t>(hardware, std::max<std::size_t>(1, sliceCount / 2));
    if (workers <= 1) {
        for (Eigen::Index i = iLo; i <= iHi; ++i) correlateOneSlice(i);
        return maps;
    }
    std::vector<std::thread> threads;
    threads.reserve(workers);
    for (std::size_t worker = 0; worker < workers; ++worker) {
        threads.emplace_back([&, worker] {
            for (std::size_t slot = worker; slot < sliceCount; slot += workers)
                correlateOneSlice(iLo + static_cast<Eigen::Index>(slot));
        });
    }
    for (std::thread& thread : threads) thread.join();
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

// Cross-slice agreement on the in-plane centre. Discriminates better than
// peak height: a real donut holds still, a noise peak wanders.
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

// Same direction along AP and IS, checking only components the prior makes
// meaningful. A dominant-axis test instead flips on the hypotenuse edge,
// whose legs are within 2.5mm of each other.
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
                                                    const FiducialDetectOptions& options,
                                                    std::vector<FiducialDetection>* rejected,
                                                    std::vector<FiducialDetection>* suppressed) {
    if (rejected != nullptr) rejected->clear();
    if (suppressed != nullptr) suppressed->clear();
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

    // Panels split on the midline, not on index order.
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
                                            isRange.second, options.maxThreads);
        std::vector<Peak> peaks = localMaxima(maps, options);
        for (Peak& peak : peaks) peak.positionMm = refine(maps, axes, peak);
        std::sort(peaks.begin(), peaks.end(),
                  [](const Peak& a, const Peak& b) { return a.response > b.response; });

        // Collapse the responses one donut produces on neighbouring slices.
        std::vector<Peak> candidates;
        for (const Peak& peak : peaks) {
            const bool capped =
                static_cast<int>(candidates.size()) >= options.candidatesPerSide;
            if (!capped) {
                const bool duplicate =
                    std::any_of(candidates.begin(), candidates.end(), [&](const Peak& kept) {
                        return (kept.positionMm - peak.positionMm).norm() <
                               options.clusterRadiusMm;
                    });
                if (!duplicate) {
                    candidates.push_back(peak);
                    continue;
                }
            }
            // Dropped here: either a weaker response from a donut already kept,
            // or past the cap. Recorded only when the caller asks, and only
            // then is the walk carried past the cap -- otherwise it stops, as
            // it always did.
            if (suppressed == nullptr) {
                if (capped) break;
                continue;
            }
            FiducialDetection other;
            other.positionMm = peak.positionMm;
            other.response = peak.response;
            other.confidence = peak.response;
            other.radiusMm = peak.radiusMm;
            // stability is left at 0: stabilityOf runs only for candidates, and
            // computing it for every suppressed peak would cost a disc search
            // per peak. Callers must not read it as a measurement.
            other.found = false;
            suppressed->push_back(other);
        }
        for (Peak& candidate : candidates) {
            candidate.stability = stabilityOf(maps, axes, candidate, options.clusterRadiusMm,
                                              3.0 * std::max(apStep, isStep));
        }

        // Assign by the prior triangle: pairwise lengths plus edge directions,
        // so the three cannot be permuted.
        const Eigen::Vector3d p0 = priorMm[static_cast<std::size_t>(group[0])];
        const Eigen::Vector3d p1 = priorMm[static_cast<std::size_t>(group[1])];
        const Eigen::Vector3d p2 = priorMm[static_cast<std::size_t>(group[2])];
        const std::array<double, 3> expected{(p0 - p1).norm(), (p0 - p2).norm(), (p1 - p2).norm()};
        const std::array<Eigen::Vector3d, 3> expectedEdges{p0 - p1, p0 - p2, p1 - p2};
        // Loose: nominal and measured AP arms differ by ~3.5mm
        // (docs/known_gaps_mri.md).
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

        if (rejected != nullptr) {
            for (int index = 0; index < count; ++index) {
                if (index == bestTriple[0] || index == bestTriple[1] || index == bestTriple[2])
                    continue;
                const Peak& peak = candidates[static_cast<std::size_t>(index)];
                FiducialDetection other;
                other.positionMm = peak.positionMm;
                other.response = peak.response;
                other.stability = peak.stability;
                other.confidence = peak.response;
                other.radiusMm = peak.radiusMm;
                // Nameless and not found: it is a place the search looked at,
                // not a marker. Callers must not treat it as one.
                other.found = false;
                rejected->push_back(other);
            }
        }
    }
    return results;
}

std::vector<FiducialDetection> detectFiducialDonutsNear(
    const Volume3D& volume, const RasAxisVectors& axes, const Eigen::Vector3d& aroundMm,
    const std::vector<Eigen::Vector3d>& avoidMm, double searchRadiusMm, const std::string& name,
    const FiducialDetectOptions& options) {
    if (axes.dimLR.size() != volume.nx || axes.dimAP.size() != volume.ny ||
        axes.dimIS.size() != volume.nz) {
        throw std::invalid_argument(
            "detectFiducialDonutNear: volume and axes disagree on dimensions");
    }
    if (searchRadiusMm <= 0.0) {
        throw std::invalid_argument("detectFiducialDonutNear needs a positive search radius");
    }

    const double apStep = axisStep(axes.dimAP);
    const double isStep = axisStep(axes.dimIS);
    const std::vector<Annulus> bank = buildAnnulusBank(options, apStep, isStep);

    const auto range = [](const Eigen::VectorXd& axis, double centre, double radius,
                          Eigen::Index n) {
        Eigen::Index a = nearestIndex(axis, centre - radius);
        Eigen::Index b = nearestIndex(axis, centre + radius);
        if (a > b) std::swap(a, b);
        return std::pair<Eigen::Index, Eigen::Index>{std::clamp<Eigen::Index>(a, 0, n - 1),
                                                     std::clamp<Eigen::Index>(b, 0, n - 1)};
    };
    const auto lrRange = range(axes.dimLR, aroundMm.x(), searchRadiusMm, volume.nx);
    const auto apRange = range(axes.dimAP, aroundMm.y(), searchRadiusMm, volume.ny);
    const auto isRange = range(axes.dimIS, aroundMm.z(), searchRadiusMm, volume.nz);

    const SideMaps maps =
        correlateSide(volume, bank, lrRange.first, lrRange.second, apRange.first, apRange.second,
                      isRange.first, isRange.second, options.maxThreads);
    std::vector<Peak> peaks = localMaxima(maps, options);

    std::vector<Peak> accepted;
    for (Peak& peak : peaks) {
        peak.positionMm = refine(maps, axes, peak);
        const double toTarget = (peak.positionMm - aroundMm).norm();
        if (toTarget > searchRadiusMm) continue;
        // Never snap onto a different marker, however generous the radius.
        const bool belongsToAnother =
            std::any_of(avoidMm.begin(), avoidMm.end(), [&](const Eigen::Vector3d& other) {
                return (peak.positionMm - other).norm() < toTarget;
            });
        if (belongsToAnother) continue;
        accepted.push_back(peak);
    }
    std::sort(accepted.begin(), accepted.end(),
              [](const Peak& a, const Peak& b) { return a.response > b.response; });

    std::vector<FiducialDetection> results;
    for (const Peak& peak : accepted) {
        const bool duplicate =
            std::any_of(results.begin(), results.end(), [&](const FiducialDetection& kept) {
                return (kept.positionMm - peak.positionMm).norm() < options.clusterRadiusMm;
            });
        if (duplicate) continue;
        FiducialDetection result;
        result.name = name;
        result.positionMm = peak.positionMm;
        result.response = peak.response;
        result.stability = stabilityOf(maps, axes, peak, options.clusterRadiusMm,
                                       3.0 * std::max(apStep, isStep));
        result.confidence = peak.response;
        result.radiusMm = peak.radiusMm;
        result.found = true;
        results.push_back(std::move(result));
    }
    return results;
}

}  // namespace beam::mri
