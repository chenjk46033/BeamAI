// detect_fiducials -- runs beam::mri::detectFiducialDonuts against the
// checked-in MRI fixture and scores it on the operator's own measurements.
//
// The GUI button calls the same library function on the same inputs; this is
// how its accuracy is checked without a display.
//
//   detect_fiducials [<mri-path> [<fiducials-csv>]]

#include <algorithm>
#include <cstdio>
#include <exception>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "array/array_data.hpp"
#include "infra_dicom/load_mri_ras.hpp"
#include "mri/fiducial_detect.hpp"
#include "mri/mri_loader.hpp"
#include "registration/array_transform.hpp"

namespace {

Eigen::MatrixXd readRectCsv(const std::string& path) {
    std::ifstream file(path);
    if (!file) throw std::runtime_error("cannot open " + path);
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
    if (rows.size() != 19) throw std::runtime_error("expected a 19-row rect CSV: " + path);
    Eigen::MatrixXd rect(19, static_cast<Eigen::Index>(rows.front().size()));
    for (Eigen::Index r = 0; r < 19; ++r)
        for (Eigen::Index c = 0; c < rect.cols(); ++c)
            rect(r, c) = rows[static_cast<std::size_t>(r)][static_cast<std::size_t>(c)];
    return rect;
}

std::map<std::string, Eigen::Vector3d> readFiducialCsv(const std::string& path) {
    std::ifstream file(path);
    if (!file) throw std::runtime_error("cannot open " + path);
    std::map<std::string, Eigen::Vector3d> measured;
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

int main(int argc, char** argv) {
    const std::string mriPath = argc > 1 ? argv[1] : BEAM_MRI_FIXTURE_NII;
    const std::string fiducialPath = argc > 2 ? argv[2] : BEAM_FIDUCIAL_FIXTURE_CSV;

    try {
        const beam::mri::MriVolumeRas loaded = beam::infra::dicom::loadMriRas(mriPath);
        const beam::mri::Volume3D volume = beam::mri::reorientedVolumeToVolume3D(loaded.volume);

        // installMri() normalises every axis ascending without reordering the
        // voxels. Detections must use the same convention or they will not
        // mean what a coordinate in the registration table means.
        beam::mri::RasAxisVectors axes = loaded.axes;
        for (Eigen::VectorXd* axis : {&axes.dimLR, &axes.dimAP, &axes.dimIS})
            if (axis->size() > 1 && (*axis)(0) > (*axis)(axis->size() - 1)) axis->reverseInPlace();

        std::printf("MRI   %s\n", mriPath.c_str());
        std::printf("dims  %lldx%lldx%lld\n", static_cast<long long>(volume.nx),
                    static_cast<long long>(volume.ny), static_cast<long long>(volume.nz));
        std::printf("LR    %.2f .. %.2f mm   AP %.2f .. %.2f   IS %.2f .. %.2f\n\n", axes.dimLR(0),
                    axes.dimLR(axes.dimLR.size() - 1), axes.dimAP(0),
                    axes.dimAP(axes.dimAP.size() - 1), axes.dimIS(0),
                    axes.dimIS(axes.dimIS.size() - 1));

        // The prior exactly as initializeRegistrationGeometry() builds it.
        beam::array::ArrayData nominal =
            beam::array::defineArrayData(readRectCsv(BEAM_DEFAULT_SUBJECT_RECT_CSV));
        beam::array::reconstructPhysicalArrayHalves(nominal);
        const Eigen::Vector3d rectCentre =
            nominal.arrayTotal.rect.block(16, 0, 3, nominal.arrayTotal.rect.cols()).rowwise().mean();
        const Eigen::Vector3d placementReference = rectCentre - Eigen::Vector3d(0.0, 0.050, -0.025);
        const Eigen::Vector3d mriCentreMeters(axes.dimLR.mean() / 1000.0, axes.dimAP.mean() / 1000.0,
                                              axes.dimIS.mean() / 1000.0);
        Eigen::Matrix4d translation = Eigen::Matrix4d::Identity();
        translation.block<3, 1>(0, 3) = mriCentreMeters - placementReference;
        const beam::registration::AffineArrayResult placed =
            beam::registration::applyAffineToArrayData(translation, nominal);

        std::vector<Eigen::Vector3d> priorMm;
        std::vector<std::string> names;
        for (const auto& marker : placed.fiducialMarkers) {
            priorMm.push_back(marker.position * 1000.0);
            names.push_back(marker.name);
        }

        const std::vector<beam::mri::FiducialDetection> detections =
            beam::mri::detectFiducialDonuts(volume, axes, priorMm, names);

        const std::map<std::string, Eigen::Vector3d> measured = readFiducialCsv(fiducialPath);
        std::printf("%-3s %-11s %9s %9s %9s %7s %7s %7s %9s\n", "#", "marker", "LR", "AP", "IS",
                    "conf", "stab", "radius", "err");
        double total = 0.0;
        int scored = 0;
        int flagged = 0;
        for (std::size_t index = 0; index < detections.size(); ++index) {
            const beam::mri::FiducialDetection& found = detections[index];
            const auto truth = measured.find(found.name);
            char error[16] = "    -";
            if (found.found && truth != measured.end()) {
                const double distance = (found.positionMm - truth->second).norm();
                std::snprintf(error, sizeof(error), "%6.2f", distance);
                total += distance;
                ++scored;
            }
            if (found.confidence < 0.75) ++flagged;
            std::printf("%-3zu %-11s %9.2f %9.2f %9.2f %6.1f%% %7.2f %7.1f %8s %s\n", index + 1,
                        found.name.c_str(), found.positionMm.x(), found.positionMm.y(),
                        found.positionMm.z(), 100.0 * found.confidence, found.stability,
                        found.radiusMm, error, found.confidence < 0.75 ? " UNTRUSTED" : "");
        }
        if (scored > 0) {
            std::printf("\nmean error %.2f mm over %d scored markers; %d flagged under 75%%\n",
                        total / scored, scored, flagged);
        }
        std::printf("\nprior (nominal placement, for reference):\n");
        for (std::size_t index = 0; index < priorMm.size(); ++index) {
            std::printf("  %-11s %9.2f %9.2f %9.2f\n", names[index].c_str(), priorMm[index].x(),
                        priorMm[index].y(), priorMm[index].z());
        }
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "detect_fiducials: %s\n", error.what());
        return 1;
    }
}
