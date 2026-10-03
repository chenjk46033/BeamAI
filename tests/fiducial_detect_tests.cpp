// Tests for beam::mri::detectFiducialDonuts -- the Registration tab's
// "Detect fiducials" button. One synthetic case that needs no data, and one
// regression against the checked-in subject that skips when the fixture or
// BeamV0's geometry is absent.

#include <array>
#include <cmath>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "array/array_data.hpp"
#include "infra_dicom/load_mri_ras.hpp"
#include "mri/fiducial_detect.hpp"
#include "mri/mri_loader.hpp"
#include "mri/slice.hpp"
#include "registration/array_transform.hpp"

namespace {

const std::vector<std::string>& markerNames() {
    static const std::vector<std::string> names{"LeftY1Z3",  "LeftY1Z1",  "LeftY4Z1",
                                                "RightY1Z3", "RightY1Z1", "RightY4Z1"};
    return names;
}

Eigen::VectorXd axisFrom(double start, double step, Eigen::Index count) {
    Eigen::VectorXd axis(count);
    for (Eigen::Index index = 0; index < count; ++index) axis(index) = start + step * index;
    return axis;
}

// A flat volume has zero local variance, which the detector skips.
double background(Eigen::Index i, Eigen::Index j, Eigen::Index k) {
    return 10.0 + 3.0 * std::sin(0.30 * i) + 3.0 * std::cos(0.21 * j) + 2.0 * std::sin(0.17 * k);
}

struct SyntheticVolume {
    beam::mri::Volume3D volume;
    beam::mri::RasAxisVectors axes;
};

// A torus, not a hollow sphere: an off-equator cut through a shell is a
// smaller but thicker ring, which the bank prefers, biasing LR by ~2.7mm.
SyntheticVolume makeVolumeWithDonuts(const std::vector<Eigen::Vector3d>& centresMm,
                                     double radiusMm) {
    constexpr double kTubeRadiusMm = 1.5;
    constexpr Eigen::Index n = 80;
    SyntheticVolume made;
    made.axes.dimLR = axisFrom(-40.0, 1.0, n);
    made.axes.dimAP = axisFrom(-40.0, 1.0, n);
    made.axes.dimIS = axisFrom(-40.0, 1.0, n);
    made.volume.nx = n;
    made.volume.ny = n;
    made.volume.nz = n;
    made.volume.kSlices.assign(static_cast<std::size_t>(n), Eigen::MatrixXd::Zero(n, n));

    for (Eigen::Index k = 0; k < n; ++k) {
        for (Eigen::Index i = 0; i < n; ++i) {
            for (Eigen::Index j = 0; j < n; ++j) {
                double value = background(i, j, k);
                const Eigen::Vector3d position(made.axes.dimLR(i), made.axes.dimAP(j),
                                               made.axes.dimIS(k));
                for (const Eigen::Vector3d& centre : centresMm) {
                    const Eigen::Vector3d offset = position - centre;
                    const double inPlane = std::hypot(offset.y(), offset.z());
                    // Axis along LR, so sagittal slices see it face-on.
                    if (std::hypot(inPlane - radiusMm, offset.x()) <= kTubeRadiusMm) value += 40.0;
                }
                made.volume.kSlices[static_cast<std::size_t>(k)](i, j) = value;
            }
        }
    }
    return made;
}

Eigen::MatrixXd readRectCsv(const char* path) {
    std::ifstream file(path);
    if (!file) return {};
    std::vector<std::vector<double>> rows;
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        std::stringstream stream(line);
        std::string cell;
        std::vector<double> row;
        while (std::getline(stream, cell, ',')) row.push_back(std::stod(cell));
        rows.push_back(std::move(row));
    }
    if (rows.size() != 19 || rows.front().empty()) return {};
    Eigen::MatrixXd rect(19, static_cast<Eigen::Index>(rows.front().size()));
    for (Eigen::Index r = 0; r < 19; ++r)
        for (Eigen::Index c = 0; c < rect.cols(); ++c)
            rect(r, c) = rows[static_cast<std::size_t>(r)][static_cast<std::size_t>(c)];
    return rect;
}

std::map<std::string, Eigen::Vector3d> readFiducialCsv(const char* path) {
    std::ifstream file(path);
    std::map<std::string, Eigen::Vector3d> measured;
    if (!file) return measured;
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::stringstream stream(line);
        std::string name, x, y, z;
        if (!std::getline(stream, name, ',')) continue;
        if (!std::getline(stream, x, ',') || !std::getline(stream, y, ',') ||
            !std::getline(stream, z, ','))
            continue;
        if (name == "name") continue;
        measured[name] = Eigen::Vector3d(std::stod(x), std::stod(y), std::stod(z));
    }
    return measured;
}

}  // namespace

TEST(FiducialDetect, RecoversSixSyntheticDonutsFromABiasedPrior) {
    const std::vector<Eigen::Vector3d> truth{{-20.0, 0.0, 10.0},  {-20.0, 0.0, -10.0},
                                             {-20.0, -20.0, -10.0}, {20.0, 0.0, 10.0},
                                             {20.0, 0.0, -10.0},  {20.0, -20.0, -10.0}};
    const SyntheticVolume made = makeVolumeWithDonuts(truth, 5.0);

    // The real prior is offset on every axis: 12mm in LR on F017, and the
    // nominal placement sits well inferior of the markers on both subjects.
    std::vector<Eigen::Vector3d> prior;
    for (const Eigen::Vector3d& centre : truth) prior.push_back(centre + Eigen::Vector3d(12, 5, 15));

    const std::vector<beam::mri::FiducialDetection> found =
        beam::mri::detectFiducialDonuts(made.volume, made.axes, prior, markerNames());

    ASSERT_EQ(found.size(), 6u);
    for (std::size_t index = 0; index < 6; ++index) {
        SCOPED_TRACE(found[index].name);
        EXPECT_EQ(found[index].name, markerNames()[index]);
        ASSERT_TRUE(found[index].found);
        EXPECT_LT((found[index].positionMm - truth[index]).norm(), 1.5);
        EXPECT_GT(found[index].confidence, 0.75);
    }
}

TEST(FiducialDetect, RefinesOneNudgedMarkerBackOntoItsDonut) {
    const std::vector<Eigen::Vector3d> truth{{-20.0, 0.0, 10.0},  {-20.0, 0.0, -10.0},
                                             {-20.0, -20.0, -10.0}, {20.0, 0.0, 10.0},
                                             {20.0, 0.0, -10.0},  {20.0, -20.0, -10.0}};
    const SyntheticVolume made = makeVolumeWithDonuts(truth, 5.0);

    // A rough manual placement, nowhere near another marker.
    const Eigen::Vector3d nudged = truth[0] + Eigen::Vector3d(2.0, 4.0, 4.0);
    const std::vector<Eigen::Vector3d> avoid(truth.begin() + 1, truth.end());

    const std::vector<beam::mri::FiducialDetection> ranked = beam::mri::detectFiducialDonutsNear(
        made.volume, made.axes, nudged, avoid, 12.0, "LeftY1Z3");
    ASSERT_FALSE(ranked.empty());
    EXPECT_LT((ranked.front().positionMm - truth[0]).norm(), 1.5);
    EXPECT_GT(ranked.front().confidence, 0.75);
}

// Refining one marker must never return another marker, even when dragged
// nearer the neighbour than its own donut.
TEST(FiducialDetect, RefiningOneMarkerNeverReturnsANeighbour) {
    const std::vector<Eigen::Vector3d> truth{{-20.0, 0.0, 10.0},  {-20.0, 0.0, -10.0},
                                             {-20.0, -20.0, -10.0}, {20.0, 0.0, 10.0},
                                             {20.0, 0.0, -10.0},  {20.0, -20.0, -10.0}};
    const SyntheticVolume made = makeVolumeWithDonuts(truth, 5.0);

    // Dragged 13mm of the 20mm gap towards LeftY1Z1: the nearest donut is now
    // the wrong one.
    const Eigen::Vector3d strayed = truth[0] + Eigen::Vector3d(0.0, 0.0, -13.0);
    const std::vector<Eigen::Vector3d> avoid(truth.begin() + 1, truth.end());

    const std::vector<beam::mri::FiducialDetection> ranked = beam::mri::detectFiducialDonutsNear(
        made.volume, made.axes, strayed, avoid, 12.0, "LeftY1Z3");
    for (const beam::mri::FiducialDetection& candidate : ranked) {
        for (std::size_t other = 1; other < truth.size(); ++other) {
            EXPECT_GT((candidate.positionMm - truth[other]).norm(), 3.0)
                << "offered marker index " << other;
        }
    }
}

TEST(FiducialDetect, RejectsAWrongNumberOfPriorPoints) {
    const SyntheticVolume made = makeVolumeWithDonuts({}, 5.0);
    const std::vector<Eigen::Vector3d> tooFew(5, Eigen::Vector3d::Zero());
    EXPECT_THROW(beam::mri::detectFiducialDonuts(made.volume, made.axes, tooFew, markerNames()),
                 std::invalid_argument);
}

// Five markers within 2mm; the sixth, ~5mm out, is the one the confidence
// score sends below the cut.
TEST(FiducialDetect, MatchesTheOperatorsMeasurementsOnTheReferenceSubject) {
    const Eigen::MatrixXd rect = readRectCsv(BEAM_DEFAULT_SUBJECT_RECT_CSV);
    const std::map<std::string, Eigen::Vector3d> measured =
        readFiducialCsv(BEAM_FIDUCIAL_FIXTURE_CSV);
    if (rect.size() == 0 || measured.size() != 6) {
        GTEST_SKIP() << "needs DefaultSubjectV0 geometry and testdata/ fiducials";
    }
    std::ifstream probe(BEAM_MRI_FIXTURE_NII, std::ios::binary);
    if (!probe) GTEST_SKIP() << "needs testdata/beamai_F040_T1_MRI.nii";
    probe.close();

    const beam::mri::MriVolumeRas loaded = beam::infra::dicom::loadMriRas(BEAM_MRI_FIXTURE_NII);
    const beam::mri::Volume3D volume = beam::mri::reorientedVolumeToVolume3D(loaded.volume);
    // installMri()'s convention: ascending axes, voxels left alone.
    beam::mri::RasAxisVectors axes = loaded.axes;
    for (Eigen::VectorXd* axis : {&axes.dimLR, &axes.dimAP, &axes.dimIS})
        if (axis->size() > 1 && (*axis)(0) > (*axis)(axis->size() - 1)) axis->reverseInPlace();

    beam::array::ArrayData nominal = beam::array::defineArrayData(rect);
    beam::array::reconstructPhysicalArrayHalves(nominal);
    const Eigen::Vector3d rectCentre =
        nominal.arrayTotal.rect.block(16, 0, 3, nominal.arrayTotal.rect.cols()).rowwise().mean();
    Eigen::Matrix4d translation = Eigen::Matrix4d::Identity();
    translation.block<3, 1>(0, 3) =
        Eigen::Vector3d(axes.dimLR.mean() / 1000.0, axes.dimAP.mean() / 1000.0,
                        axes.dimIS.mean() / 1000.0) -
        (rectCentre - Eigen::Vector3d(0.0, 0.050, -0.025));
    const beam::registration::AffineArrayResult placed =
        beam::registration::applyAffineToArrayData(translation, nominal);

    std::vector<Eigen::Vector3d> prior;
    std::vector<std::string> names;
    for (const auto& marker : placed.fiducialMarkers) {
        prior.push_back(marker.position * 1000.0);
        names.push_back(marker.name);
    }

    const std::vector<beam::mri::FiducialDetection> found =
        beam::mri::detectFiducialDonuts(volume, axes, prior, names);
    ASSERT_EQ(found.size(), 6u);

    int within2mm = 0;
    int flagged = 0;
    for (const beam::mri::FiducialDetection& detection : found) {
        SCOPED_TRACE(detection.name);
        ASSERT_TRUE(detection.found);
        const auto truth = measured.find(detection.name);
        ASSERT_NE(truth, measured.end());
        const double error = (detection.positionMm - truth->second).norm();
        const bool trusted = detection.confidence >= 0.75;
        if (error <= 2.0) ++within2mm;
        if (!trusted) ++flagged;
        // Anything the score vouches for must be accurate.
        if (trusted) EXPECT_LE(error, 2.0);
    }
    EXPECT_EQ(within2mm, 5);
    EXPECT_EQ(flagged, 1);
    EXPECT_LT(found[2].confidence, 0.75) << "LeftY4Z1 is the known-bad marker";
}
