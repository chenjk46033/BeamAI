#include "startup_data.hpp"

#include <cmath>
#include <fstream>
#include <numbers>
#include <sstream>
#include <stdexcept>

#include <QCoreApplication>

#include "infra_dicom/load_mri_ras.hpp"
#include "mri/mri_loader.hpp"

namespace beam::app {

Eigen::VectorXd syntheticRf(double echoAmplitude) {
    constexpr int kN = 1600;
    Eigen::VectorXd v = Eigen::VectorXd::Zero(kN);
    const auto addBurst = [&](double center, double width, double amp) {
        for (int i = 0; i < kN; ++i) {
            const double dt = i - center;
            v(i) += amp * std::exp(-(dt * dt) / (2.0 * width * width)) *
                    std::sin(2.0 * std::numbers::pi * i / 6.0);
        }
    };
    addBurst(260.0, 12.0, 60.0);
    addBurst(780.0, 45.0, echoAmplitude);
    return v;
}

Eigen::MatrixXd syntheticArrayRect(int n) {
    constexpr double kSpacing = 0.002, kZSpacing = 0.0007, kHalf = 0.006;
    Eigen::MatrixXd rect = Eigen::MatrixXd::Zero(19, n);
    const double mid = static_cast<double>(n) / 2.0;
    for (int i = 0; i < n; ++i) {
        const Eigen::Vector3d c((static_cast<double>(i + 1) - mid) * kSpacing, (static_cast<double>(i) - mid) * 0.001,
                                 (static_cast<double>(i) - mid) * kZSpacing);
        rect(0, i) = i + 1;
        rect.block<3, 1>(1, i) = c + Eigen::Vector3d(-kHalf, -kHalf, 0);
        rect.block<3, 1>(4, i) = c + Eigen::Vector3d(kHalf, -kHalf, 0);
        rect.block<3, 1>(7, i) = c + Eigen::Vector3d(kHalf, kHalf, 0);
        rect.block<3, 1>(10, i) = c + Eigen::Vector3d(-kHalf, kHalf, 0);
        rect.block<3, 1>(16, i) = c;
    }
    return rect;
}

beam::mri::Volume3D syntheticMriVolume(Eigen::Index nx, Eigen::Index ny, Eigen::Index nz) {
    beam::mri::Volume3D v;
    v.nx = nx;
    v.ny = ny;
    v.nz = nz;
    v.kSlices.resize(static_cast<std::size_t>(nz));
    const double cx = static_cast<double>(nx) / 2.0;
    const double cy = static_cast<double>(ny) / 2.0;
    const double cz = static_cast<double>(nz) / 2.0;
    const double sigma = std::min({cx, cy, cz}) * 0.7;
    for (Eigen::Index k = 0; k < nz; ++k) {
        Eigen::MatrixXd slice(nx, ny);
        for (Eigen::Index i = 0; i < nx; ++i) {
            for (Eigen::Index j = 0; j < ny; ++j) {
                const double dx = static_cast<double>(i) - cx;
                const double dy = static_cast<double>(j) - cy;
                const double dz = static_cast<double>(k) - cz;
                const double r2 = dx * dx + dy * dy + dz * dz;
                slice(i, j) = 255.0 * std::exp(-r2 / (2.0 * sigma * sigma));
            }
        }
        v.kSlices[static_cast<std::size_t>(k)] = slice;
    }
    return v;
}

MriLoadResult tryLoadMri(const std::string& path) {
    MriLoadResult r;
    try {
        const beam::mri::MriVolumeRas ras = beam::infra::dicom::loadMriRas(path);
        r.volume = beam::mri::reorientedVolumeToVolume3D(ras.volume);
        r.axes = ras.axes;
    } catch (const std::exception& e) {
        r.error = e.what();
    }
    return r;
}

std::vector<std::string> defaultSubjectMriCandidates() {
    const QString exeDir = QCoreApplication::applicationDirPath();
    return {
        (exeDir + "/../../../../DefaultSubjectV0/defaultSubjectMNIV1.nii").toStdString(),
        "../DefaultSubjectV0/defaultSubjectMNIV1.nii",
    };
}

std::vector<std::string> defaultSubjectArrayRectCandidates() {
    const QString exeDir = QCoreApplication::applicationDirPath();
    return {
        (exeDir + "/../../../../DefaultSubjectV0/defaultSubjectArrayRect.csv").toStdString(),
        "../DefaultSubjectV0/defaultSubjectArrayRect.csv",
    };
}

Eigen::MatrixXd readCsvMatrix(const std::string& path, Eigen::Index expectedRows) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("readCsvMatrix: cannot open '" + path + "'");
    }
    std::vector<std::vector<double>> rows;
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        std::vector<double> row;
        std::stringstream ss(line);
        std::string cell;
        while (std::getline(ss, cell, ',')) {
            row.push_back(std::stod(cell));
        }
        rows.push_back(std::move(row));
    }
    if (static_cast<Eigen::Index>(rows.size()) != expectedRows) {
        throw std::runtime_error("readCsvMatrix: '" + path + "' has " + std::to_string(rows.size()) +
                                  " rows, expected " + std::to_string(expectedRows));
    }
    const Eigen::Index cols = static_cast<Eigen::Index>(rows.front().size());
    Eigen::MatrixXd m(expectedRows, cols);
    for (Eigen::Index r = 0; r < expectedRows; ++r) {
        if (static_cast<Eigen::Index>(rows[static_cast<std::size_t>(r)].size()) != cols) {
            throw std::runtime_error("readCsvMatrix: '" + path + "' has a ragged row");
        }
        for (Eigen::Index c = 0; c < cols; ++c) {
            m(r, c) = rows[static_cast<std::size_t>(r)][static_cast<std::size_t>(c)];
        }
    }
    return m;
}

std::vector<std::string> defaultSubjectFiducialMarkersCandidates() {
    const QString exeDir = QCoreApplication::applicationDirPath();
    return {
        (exeDir + "/../../../../DefaultSubjectV0/defaultSubjectFiducialMarkers.csv").toStdString(),
        "../DefaultSubjectV0/defaultSubjectFiducialMarkers.csv",
    };
}

std::vector<std::pair<std::string, Eigen::Vector3d>> defaultSubjectFiducialMarkers() {
    for (const std::string& candidate : defaultSubjectFiducialMarkersCandidates()) {
        std::ifstream file(candidate);
        if (!file) continue;
        std::vector<std::pair<std::string, Eigen::Vector3d>> markers;
        std::string line;
        std::getline(file, line);  // header
        while (std::getline(file, line)) {
            if (line.empty()) continue;
            std::stringstream ss(line);
            std::string name, xs, ys, zs;
            std::getline(ss, name, ',');
            std::getline(ss, xs, ',');
            std::getline(ss, ys, ',');
            std::getline(ss, zs, ',');
            markers.emplace_back(name, Eigen::Vector3d(std::stod(xs), std::stod(ys), std::stod(zs)));
        }
        if (!markers.empty()) return markers;
    }
    return {};
}

std::pair<Eigen::MatrixXd, std::string> defaultSubjectArrayRect() {
    for (const std::string& candidate : defaultSubjectArrayRectCandidates()) {
        try {
            Eigen::MatrixXd rect = readCsvMatrix(candidate, kArrayRectRows);
            return {std::move(rect), "default-subject:" + candidate};
        } catch (const std::exception&) {
            continue;
        }
    }
    return {syntheticArrayRect(90), "synthetic"};
}

DefaultSubjectCrf defaultSubjectCrf() {
    return DefaultSubjectCrf{QStringLiteral("Utah"), QStringLiteral("C001"), QStringLiteral("1")};
}

std::vector<double> syntheticShamBurstSound() {
    constexpr double kToneHz = 200.0;
    constexpr double kBurstSeconds = 0.05;
    const std::size_t n = static_cast<std::size_t>(kBurstSeconds * kShamSoundFs);
    std::vector<double> sound(n);
    for (std::size_t i = 0; i < n; ++i) {
        sound[i] = std::sin(2.0 * std::numbers::pi * kToneHz * static_cast<double>(i) / kShamSoundFs);
    }
    return sound;
}

beam::gui_qt::TreatmentProtocolRow toRow(const beam::gui::TreatmentProtocolRecord& t) {
    beam::gui_qt::TreatmentProtocolRow r;
    r.number = t.number;
    r.target = QString::fromStdString(t.target);
    r.duration = t.duration;
    r.amplitude = t.amplitude;
    r.parameters = QString::fromStdString(t.parameters);
    r.responsePain = t.pain;
    r.responseMood = t.mood;
    r.notes = QString::fromStdString(t.notes);
    return r;
}

beam::gui::TreatmentProtocolRecord toRecord(const beam::gui_qt::TreatmentProtocolRow& r) {
    beam::gui::TreatmentProtocolRecord t;
    t.number = r.number;
    t.target = r.target.toStdString();
    t.duration = r.duration;
    t.amplitude = r.amplitude;
    t.parameters = r.parameters.toStdString();
    t.pain = r.responsePain;
    t.mood = r.responseMood;
    t.notes = r.notes.toStdString();
    return t;
}

}  // namespace beam::app
