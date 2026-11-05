#include "mri/fingerprint.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <sstream>

namespace beam::mri {
namespace {

// Enough samples to separate two heads, few enough that a 124 MB scan is still
// fingerprinted in well under a second.
constexpr std::int64_t kTargetSamples = 65536;

constexpr std::uint64_t kFnvOffset = 1469598103934665603ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

void mix(std::uint64_t& hash, std::uint64_t value) {
    for (int byte = 0; byte < 8; ++byte) {
        hash ^= (value >> (byte * 8)) & 0xFFULL;
        hash *= kFnvPrime;
    }
}

// Quantised before hashing: a value reread from the same file is identical,
// but rounding at the last bit should not make a scan look like a new one.
std::uint64_t quantise(double value) {
    if (!std::isfinite(value)) return 0ULL;
    return static_cast<std::uint64_t>(static_cast<std::int64_t>(std::llround(value * 1000.0)));
}

std::string span(const Eigen::VectorXd& axis) {
    std::ostringstream out;
    out.setf(std::ios::fixed);
    out.precision(3);
    if (axis.size() < 2) {
        out << axis.size() << ":0.000..0.000";
        return out.str();
    }
    out << axis.size() << ':' << axis(0) << ".." << axis(axis.size() - 1);
    return out.str();
}

}  // namespace

std::string geometryFingerprint(const RasAxisVectors& axes) {
    if (axes.dimLR.size() < 2) return {};
    return span(axes.dimLR) + ';' + span(axes.dimAP) + ';' + span(axes.dimIS);
}

std::string volumeFingerprint(const Volume3D& volume, const RasAxisVectors& axes) {
    const std::string geometry = geometryFingerprint(axes);
    if (geometry.empty() || volume.nx < 1 || volume.ny < 1 || volume.nz < 1) return {};
    if (static_cast<std::int64_t>(volume.kSlices.size()) < volume.nz) return {};

    const std::int64_t total =
        static_cast<std::int64_t>(volume.nx) * volume.ny * volume.nz;
    // A stride rather than a block, so the samples are spread through the head
    // instead of sitting in one corner of air.
    const std::int64_t stride = total > kTargetSamples ? total / kTargetSamples : 1;

    std::uint64_t hash = kFnvOffset;
    std::int64_t taken = 0;
    for (std::int64_t flat = 0; flat < total; flat += stride) {
        const std::int64_t k = flat / (volume.nx * volume.ny);
        const std::int64_t rest = flat - k * volume.nx * volume.ny;
        const std::int64_t j = rest / volume.nx;
        const std::int64_t i = rest - j * volume.nx;
        if (k >= volume.nz || j >= volume.ny || i >= volume.nx) break;
        mix(hash, quantise(volume(i, j, k)));
        ++taken;
    }
    mix(hash, static_cast<std::uint64_t>(taken));

    std::ostringstream out;
    out << geometry << ";v" << std::hex << hash;
    return out.str();
}

bool fingerprintsMatch(const std::string& a, const std::string& b) {
    if (a.empty() || b.empty()) return false;
    return a == b;
}

}  // namespace beam::mri
