// detect_fiducials -- runs beam::mri::detectFiducialDonuts against the
// checked-in MRI fixture and scores it on the operator's own measurements.
//
// The GUI button calls the same library function on the same inputs; this is
// how its accuracy is checked without a display.
//
//   detect_fiducials [<mri-path> [<fiducials-csv>]]

#include <algorithm>
#include <chrono>
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

        // argv[5] = a fiducials CSV to use AS THE PRIOR, reproducing what the
        // app does after a session load (registrationSourceFiducials_ holds
        // the session's own points, not the nominal placement).
        if (argc > 5) {
            const std::map<std::string, Eigen::Vector3d> seed = readFiducialCsv(argv[5]);
            for (std::size_t n = 0; n < names.size(); ++n) {
                const auto found = seed.find(names[n]);
                if (found != seed.end()) priorMm[n] = found->second;
            }
            std::printf("prior taken from %s\n", argv[5]);
        }

        beam::mri::FiducialDetectOptions options;
        if (argc > 3) options.lrMarginMm = std::stod(argv[3]);
        if (argc > 4) options.maxThreads = static_cast<unsigned>(std::stoul(argv[4]));
        std::printf("LR search margin %.1f mm, threads %u\n\n", options.lrMarginMm,
                    options.maxThreads);

        const auto started = std::chrono::steady_clock::now();
        const std::vector<beam::mri::FiducialDetection> detections =
            beam::mri::detectFiducialDonuts(volume, axes, priorMm, names, options);
        std::printf("detection took %.0f ms\n\n",
                    std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - started).count());

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
        // What the per-row "Detect this fiducial" does, from three starts.
        std::printf("\nper-marker refine (12 mm radius), from three starting points:\n");
        std::printf("%-3s %-11s %18s %18s %18s\n", "#", "marker", "from detection",
                    "from measured+5mm", "from nominal prior");
        for (std::size_t index = 0; index < detections.size(); ++index) {
            const beam::mri::FiducialDetection& found = detections[index];
            const auto truth = measured.find(found.name);
            if (truth == measured.end()) continue;

            std::vector<Eigen::Vector3d> avoid;
            for (std::size_t other = 0; other < detections.size(); ++other)
                if (other != index) avoid.push_back(detections[other].positionMm);

            const auto describe = [&](const Eigen::Vector3d& start) {
                const std::vector<beam::mri::FiducialDetection> ranked =
                    beam::mri::detectFiducialDonutsNear(volume, axes, start, avoid, 12.0,
                                                        found.name);
                const beam::mri::FiducialDetection refined =
                    ranked.empty() ? beam::mri::FiducialDetection{} : ranked.front();
                char text[32];
                if (!refined.found) {
                    std::snprintf(text, sizeof(text), "%17s", "not found");
                } else {
                    std::snprintf(text, sizeof(text), "err %5.2f mv %5.2f",
                                  (refined.positionMm - truth->second).norm(),
                                  (refined.positionMm - start).norm());
                }
                return std::string(text);
            };

            std::printf("%-3zu %-11s %18s %18s %18s\n", index + 1, found.name.c_str(),
                        describe(found.positionMm).c_str(),
                        describe(truth->second + Eigen::Vector3d(2.0, 4.0, 3.0)).c_str(),
                        describe(priorMm[index]).c_str());
        }
        std::printf("err = mm from the operator's measurement, mv = mm it moved from the start\n");

        // Ranked local candidates: is the true marker ever the runner-up?
        std::printf("\nranked candidates within 12 mm of each detected point:\n");
        for (std::size_t index = 0; index < detections.size(); ++index) {
            const auto truth = measured.find(detections[index].name);
            if (truth == measured.end()) continue;
            std::vector<Eigen::Vector3d> avoid;
            for (std::size_t other = 0; other < detections.size(); ++other)
                if (other != index) avoid.push_back(detections[other].positionMm);
            const std::vector<beam::mri::FiducialDetection> ranked =
                beam::mri::detectFiducialDonutsNear(volume, axes, detections[index].positionMm,
                                                    avoid, 12.0, detections[index].name);
            std::printf("  %-11s", detections[index].name.c_str());
            if (ranked.empty()) std::printf("  (none)");
            for (std::size_t rank = 0; rank < ranked.size() && rank < 4; ++rank) {
                std::printf("  #%zu %.3f err %.2f", rank + 1, ranked[rank].confidence,
                            (ranked[rank].positionMm - truth->second).norm());
            }
            std::printf("\n");
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
