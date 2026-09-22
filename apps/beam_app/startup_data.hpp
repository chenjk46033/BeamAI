#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <QString>
#include <Eigen/Core>

#include "gui/session_io.hpp"
#include "gui_qt/treatment_protocol_table_model.hpp"
#include "mri/ras_transform.hpp"
#include "mri/slice.hpp"

// Helpers main() uses to seed synthetic/default-subject state and convert
// between Qt models and domain records.
namespace beam::app {

Eigen::VectorXd syntheticRf(double echoAmplitude);
Eigen::MatrixXd syntheticArrayRect(int n);
beam::mri::Volume3D syntheticMriVolume(Eigen::Index nx, Eigen::Index ny, Eigen::Index nz);

struct MriLoadResult {
    beam::mri::Volume3D volume;
    std::optional<beam::mri::RasAxisVectors> axes;
    std::string error;  // empty on success
};

MriLoadResult tryLoadMri(const std::string& path);

std::vector<std::string> defaultSubjectMriCandidates();
std::vector<std::string> defaultSubjectArrayRectCandidates();

inline constexpr Eigen::Index kArrayRectRows = 19;

Eigen::MatrixXd readCsvMatrix(const std::string& path, Eigen::Index expectedRows);
std::pair<Eigen::MatrixXd, std::string> defaultSubjectArrayRect();

// Real calibrated fiducial positions (name + mm), exported from defaultSubjectMNIV1.mat. Empty if not found.
std::vector<std::string> defaultSubjectFiducialMarkersCandidates();
std::vector<std::pair<std::string, Eigen::Vector3d>> defaultSubjectFiducialMarkers();

struct DefaultSubjectCrf {
    QString siteId;
    QString participantId;
    QString visitNumber;
};

DefaultSubjectCrf defaultSubjectCrf();

inline constexpr double kShamSoundFs = 44100.0;
std::vector<double> syntheticShamBurstSound();

beam::gui_qt::TreatmentProtocolRow toRow(const beam::gui::TreatmentProtocolRecord& t);
beam::gui::TreatmentProtocolRecord toRecord(const beam::gui_qt::TreatmentProtocolRow& r);

}  // namespace beam::app
