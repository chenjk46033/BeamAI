// sweep_registration -- runs the Registration tab's whole sequence headlessly
// for one MRI: auto-detect the six fiducials, "Register to MRI fiducials",
// then "Register Arrays to Current Position" at a given lock notch.
//
// The point is a dataset sweep: the GUI does exactly these calls on exactly
// these inputs, so the array centre printed here is the one the Array position
// readout shows. Emits machine-readable rows plus a fiducial CSV that
// matlab_verify/beamv0_register_from_beamai.m can feed to BeamV0.
//
//   sweep_registration <mri-path> <label> <out-dir> [lock-notch]

#include <cmath>
#include <cstdio>
#include <exception>
#include <fstream>
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

Eigen::Vector3d arrayCentreMm(const beam::array::ArrayData& data) {
    return data.arrayTotal.rect.block(16, 0, 3, data.arrayTotal.rect.cols()).rowwise().mean() * 1000.0;
}

// registration_fiducial_layout.cpp's nominal plate: a 20.0 mm vertical arm
// (two 10 mm notches) and a 22.5 mm horizontal one (three 7.5 mm notches).
double nominalSideMm(int from, int to) {
    const bool vertical = (from % 3 == 0 && to % 3 == 1) || (from % 3 == 1 && to % 3 == 0);
    const bool horizontal = (from % 3 == 1 && to % 3 == 2) || (from % 3 == 2 && to % 3 == 1);
    if (vertical) return 20.0;
    if (horizontal) return 22.5;
    return std::hypot(20.0, 22.5);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: sweep_registration <mri-path> <label> <out-dir> [lock-notch]\n");
        return 2;
    }
    const std::string mriPath = argv[1];
    const std::string label = argv[2];
    const std::string outDir = argv[3];
    const double notch = argc > 4 ? std::stod(argv[4]) : 3.0;

    try {
        const beam::mri::MriVolumeRas loaded = beam::infra::dicom::loadMriRas(mriPath);
        const beam::mri::Volume3D volume = beam::mri::reorientedVolumeToVolume3D(loaded.volume);
        beam::mri::RasAxisVectors axes = loaded.axes;
        for (Eigen::VectorXd* axis : {&axes.dimLR, &axes.dimAP, &axes.dimIS})
            if (axis->size() > 1 && (*axis)(0) > (*axis)(axis->size() - 1)) axis->reverseInPlace();

        // initializeRegistrationGeometry(), verbatim.
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
        beam::registration::AffineArrayResult placed =
            beam::registration::applyAffineToArrayData(translation, nominal);
        const beam::array::ArrayData originArrayData = placed.arrayData;

        std::vector<Eigen::Vector3d> priorMm;
        std::vector<std::string> names;
        for (const auto& marker : placed.fiducialMarkers) {
            priorMm.push_back(marker.position * 1000.0);
            names.push_back(marker.name);
        }

        const beam::mri::FiducialDetectOptions options;
        const std::vector<beam::mri::FiducialDetection> detections =
            beam::mri::detectFiducialDonuts(volume, axes, priorMm, names, options);

        std::vector<Eigen::Vector3d> measured;
        for (std::size_t i = 0; i < detections.size(); ++i) {
            const beam::mri::FiducialDetection& d = detections[i];
            // A marker the detector could not place keeps the prior, which is
            // what the table shows and therefore what Register would fit.
            measured.push_back(d.found ? d.positionMm : priorMm[i]);
            std::printf("DET,%s,%zu,%s,%.4f,%.4f,%.4f,%.4f,%.4f,%.2f,%d\n", label.c_str(), i + 1,
                        d.name.c_str(), measured.back().x(), measured.back().y(), measured.back().z(),
                        d.confidence, d.stability, d.radiusMm, d.found ? 1 : 0);
        }
        if (measured.size() != 6) throw std::runtime_error("detector returned != 6 markers");

        for (int base : {0, 3}) {
            for (int k = 0; k < 3; ++k) {
                const int from = base + k;
                const int to = base + (k + 1) % 3;
                std::printf("SIDE,%s,%s,%s,%.4f,%.4f\n", label.c_str(), names[from].c_str(),
                            names[to].c_str(), (measured[from] - measured[to]).norm(),
                            nominalSideMm(from, to));
            }
        }

        const Eigen::Vector3d fit =
            arrayCentreMm(beam::registration::registerArrayToFiducials(originArrayData, measured).arrayData);
        std::printf("FIT,%s,%.12f,%.12f,%.12f\n", label.c_str(), fit.x(), fit.y(), fit.z());

        const Eigen::Vector3d lock = arrayCentreMm(
            beam::registration::registerCurrentTransducerPosition(originArrayData, measured, notch, notch)
                .arrayData);
        std::printf("LOCK,%s,%.12f,%.12f,%.12f\n", label.c_str(), lock.x(), lock.y(), lock.z());

        const std::string csvPath = outDir + "/beamai_fiducials_" + label + ".csv";
        std::ofstream out(csvPath);
        if (!out) throw std::runtime_error("cannot write " + csvPath);
        out << "# BeamAI auto-detected fiducials, millimetres, RAS\n";
        out << "# MRI: " << mriPath << "\n";
        out << "name,x,y,z\n";
        out.precision(17);
        for (std::size_t i = 0; i < measured.size(); ++i)
            out << names[i] << ',' << measured[i].x() << ',' << measured[i].y() << ','
                << measured[i].z() << "\n";
        std::printf("CSV,%s,%s\n", label.c_str(), csvPath.c_str());
        return 0;
    } catch (const std::exception& error) {
        std::printf("FAIL,%s,%s\n", label.c_str(), error.what());
        return 1;
    }
}
