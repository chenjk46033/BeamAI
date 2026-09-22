#pragma once

#include <QMainWindow>

#include "gui/treatment_workflow.hpp"
#include "array/array_types.hpp"
#include "mri/slice.hpp"
#include "mri/ras_transform.hpp"
#include "registration/fiducial_markers.hpp"

QT_BEGIN_NAMESPACE
namespace Ui { class WorkflowShell; }
QT_END_NAMESPACE

namespace beam::app {

class WorkflowWindow final : public QMainWindow {
public:
    explicit WorkflowWindow(QWidget* parent = nullptr);
    ~WorkflowWindow() override;

private:
    void selectStage(beam::gui::WorkflowStage stage);
    void completeCurrentStage();
    void chooseNifti();
    void chooseDicomDirectory();
    void loadMri(const QString& path);
    void showMriPreviews();
    void initializeRegistrationGeometry();
    void refresh();
    void showMessage(const QString& text, bool error);

    Ui::WorkflowShell* ui_;
    beam::gui::TreatmentWorkflow workflow_;
    beam::gui::WorkflowStage selectedStage_ = beam::gui::WorkflowStage::CaseSetup;
    beam::mri::Volume3D mriVolume_;
    beam::mri::RasAxisVectors mriAxes_;
    beam::array::ArrayData arrayData_;
    beam::mri::Volume3D arrayMask_;
    std::vector<beam::registration::FiducialMarker> fiducials_;
    Eigen::Vector3d targetMm_ = Eigen::Vector3d::Zero();
    bool registrationGeometryLoaded_ = false;
    bool mriLoaded_ = false;
    QString mriPath_;
};

}  // namespace beam::app
