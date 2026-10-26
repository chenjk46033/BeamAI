#include "treatment_internal.hpp"

namespace beam::app {

void WorkflowWindow::saveTreatmentSessionFile() {
    beam::gui::Patient open;
    if (!patients_.find(currentPatientId_, &open)) {
        showMessage(QStringLiteral("Accept a case before saving a session."), true);
        return;
    }
    // Written out first, so the session and the device agree.
    savePatientData();

    beam::gui::TreatmentSessionFile session;
    session.participantId = open.externalId;
    session.siteId = open.site;
    session.mriPath = mriPath_.toStdString();
    session.appVersion = QCoreApplication::applicationVersion().toStdString();
    session.treatmentLogCsv = patientFileText(QStringLiteral("treatment_log.csv")).toStdString();
    session.visitStateCsv =
        patientFileText(QStringLiteral("treatment_visit_state.csv")).toStdString();
    session.targetsCsv = patientFileText(QStringLiteral("treatment_targets.csv")).toStdString();
    if (session.targetsCsv.empty()) {
        // So the session is self-contained wherever it is opened.
        QFile seed(shippedDataFile(QStringLiteral("treatment_targets.csv")));
        if (seed.open(QIODevice::ReadOnly | QIODevice::Text))
            session.targetsCsv = QString::fromUtf8(seed.readAll()).toStdString();
    }

    session.fiducials = fiducials_;
    session.fiducialLocated = fiducialLocated_;
    session.fiducialConfirmed = fiducialConfirmed_;
    session.fiducialConfidence = fiducialConfidence_;
    session.fiducialStability = fiducialStability_;
    if (leftHorizontalPositionSlider_ != nullptr) {
        session.lock.leftHorizontal = leftHorizontalPositionSlider_->value();
        session.lock.leftVertical = leftVerticalPositionSlider_->value();
        session.lock.rightHorizontal = rightHorizontalPositionSlider_->value();
        session.lock.rightVertical = rightVerticalPositionSlider_->value();
    }
    const auto complete = [this](beam::gui::WorkflowStage stage) {
        return workflow_.state(stage).status == beam::gui::WorkflowStatus::Complete;
    };
    session.imagingAccepted = complete(beam::gui::WorkflowStage::Imaging);
    session.registrationAccepted = complete(beam::gui::WorkflowStage::Registration);
    if (mriLoaded_) {
        session.mri.present = true;
        session.mri.volume = mriVolume_;
        session.mri.axes = mriAxes_;
    }

    // Beside the MRIs, where BeamV0 puts them; CRF auto-save name.
    const beam::gui::CaseReportForm crf = reportFormForVisit(currentVisitNumber());
    const QString suggested =
        QDir(imagingStartDirectory())
            .filePath(caseReportFileStem(open, crf, currentVisitNumber()) +
                      QStringLiteral(".beamsession"));
    const QString path =
        QFileDialog::getSaveFileName(this, QStringLiteral("Save session"), suggested,
                                     QStringLiteral("Beam session (*.beamsession)"));
    if (path.isEmpty()) return;
    try {
        beam::gui::writeTreatmentSession(path.toStdString(), session);
    } catch (const std::exception& error) {
        showMessage(QStringLiteral("The session could not be saved: %1").arg(error.what()), true);
        return;
    }
    showMessage(QStringLiteral("Session saved to %1%2")
                    .arg(QDir::toNativeSeparators(path),
                         session.mri.present ? QString()
                                             : QStringLiteral(" (no MRI was loaded to include)")),
                false);
}

void WorkflowWindow::chooseTreatmentSession() {
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Load session"), imagingStartDirectory(),
        QStringLiteral("Beam session (*.beamsession);;All files (*)"));
    if (path.isEmpty()) return;
    rememberImagingDirectory(path);

    beam::gui::TreatmentSessionFile session;
    try {
        session = beam::gui::readTreatmentSession(path.toStdString());
    } catch (const std::exception& error) {
        QMessageBox::critical(this, QStringLiteral("Load session"),
                              QString::fromUtf8(error.what()));
        showMessage(QStringLiteral("The session was not loaded; nothing was changed."), true);
        return;
    }
    loadTreatmentSessionFile(session, path);
}

void WorkflowWindow::loadTreatmentSessionFile(const beam::gui::TreatmentSessionFile& session,
                                              const QString& path) {
    if (deliveryRunning()) {
        showMessage(QStringLiteral("A sonication is still going out. Stop it before loading a "
                                   "session."),
                    true);
        return;
    }
    const QString participant = QString::fromStdString(session.participantId);

    // Runs the whole patient switch: outgoing written out, theirs cleared.
    beam::gui::Patient details;
    details.externalId = session.participantId;
    details.site = session.siteId;
    selectPatient(details, /*advancing=*/true);
    beam::gui::Patient opened;
    if (!patients_.find(currentPatientId_, &opened) ||
        opened.externalId != session.participantId) {
        showMessage(QStringLiteral("%1 could not be opened; the session was not loaded.")
                        .arg(participant),
                    true);
        return;
    }

    // The session IS the case record, so loading it accepts it.
    workflow_.complete(beam::gui::WorkflowStage::CaseSetup, nullptr);
    workflow_.complete(beam::gui::WorkflowStage::SystemCheck, nullptr);

    const QString directory = patientDataDirectory();
    // No prompt, as BeamV0; what is replaced is renamed aside, not destroyed.
    const QString stamp =
        QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    int setAside = 0;
    const auto put = [&](const QString& name, const std::string& text) {
        if (text.empty()) return;
        const QString target = QDir(directory).filePath(name);
        if (QFileInfo::exists(target) &&
            QFile::rename(target, QStringLiteral("%1.replaced-%2").arg(target, stamp))) {
            ++setAside;
        }
        QFile file(target);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
            file.write(QByteArray::fromStdString(text));
    };
    put(QStringLiteral("treatment_log.csv"), session.treatmentLogCsv);
    put(QStringLiteral("treatment_visit_state.csv"), session.visitStateCsv);
    put(QStringLiteral("treatment_targets.csv"), session.targetsCsv);
    loadPatientData();

    if (!session.mri.present) {
        showMessage(QStringLiteral("Session loaded for %1. It carries no MRI, so imaging and "
                                   "registration still have to be done.")
                        .arg(participant),
                    false);
        return;
    }

    // BeamV0 clears both registration flags on load; the head has moved.
    installMri(session.mri.volume, session.mri.axes,
               QString::fromStdString(session.mriPath));
    fiducials_ = session.fiducials;
    registrationSourceFiducials_ = fiducials_;
    fiducialLocated_ = session.fiducialLocated;
    // Confirming the restored marks is the first step of registering again.
    fiducialConfirmed_.fill(false);
    fiducialDetected_ = fiducialLocated_;
    fiducialConfidence_ = session.fiducialConfidence;
    fiducialStability_ = session.fiducialStability;
    sourceFiducialLocated_ = fiducialLocated_;
    sourceFiducialConfirmed_ = fiducialConfirmed_;
    for (QSlider* slider : {leftHorizontalPositionSlider_, leftVerticalPositionSlider_,
                            rightHorizontalPositionSlider_, rightVerticalPositionSlider_}) {
        if (slider == nullptr) continue;
        const QSignalBlocker blocker(slider);
        slider->setValue(slider == leftHorizontalPositionSlider_ ? session.lock.leftHorizontal
                         : slider == leftVerticalPositionSlider_ ? session.lock.leftVertical
                         : slider == rightHorizontalPositionSlider_
                             ? session.lock.rightHorizontal
                             : session.lock.rightVertical);
    }
    populateRegistrationTable();
    showMriPreviews();

    QString restored = QStringLiteral("Session loaded for %1.").arg(participant);
    if (session.imagingAccepted) workflow_.complete(beam::gui::WorkflowStage::Imaging, nullptr);
    // Nothing was invalidated: the load left this stage waiting to be done.
    workflow_.begin(beam::gui::WorkflowStage::Registration, nullptr);
    restored += fiducials_.size() == 6
                    ? QStringLiteral(" MRI and fiducials restored; register the array and accept.")
                    : QStringLiteral(" MRI restored; the fiducials still have to be placed.");
    // Never restored: gel dries and the subject moves.
    clearCouplingMeasurement();
    goToNextStage();
    refresh();
    // A load already on this page arrives at nothing, so jump explicitly.
    if (!fiducials_.empty() && selectedStage_ == beam::gui::WorkflowStage::Registration) {
        ui_->registrationTable->setCurrentCell(0, 0);
        registrationStartFiducialRow_ = 0;
        ui_->registrationReturnStartButton->setEnabled(true);
        // Deferred: panning needs the final view size.
        QTimer::singleShot(0, this, [this] { navigateToRegistrationFiducial(0); });
    }
    showMessage(restored + QStringLiteral(" Coupling must be measured again before sonicating."),
                false);
    Q_UNUSED(path);
}

}  // namespace beam::app
