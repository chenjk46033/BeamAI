#include "workflow_internal.hpp"

namespace beam::app {

void WorkflowWindow::goToNextStage() {
    const int count = ui_->stageList->count();
    if (count == 0) return;
    int row = std::clamp(static_cast<int>(workflow_.nextStage()), 0, count - 1);
    while (row > 0 && ui_->stageList->item(row)->isHidden()) --row;
    // Only if row 0 itself were hidden, which it is not; forwards is the honest fallback if
    while (row < count && ui_->stageList->item(row)->isHidden()) ++row;
    if (row < count) ui_->stageList->setCurrentRow(row);
}

// Dark text on the light input background.
void WorkflowWindow::styleInputField(QLineEdit* edit) {
    if (edit == nullptr) return;
    edit->setStyleSheet(QStringLiteral(
        "QLineEdit { background: #ffffff; color: #17313f; selection-background-color: #58c7e5; "
        "selection-color: #083d4d; } QLineEdit:disabled { color: #65757d; background: #edf1f2; }"));
    QPalette palette = edit->palette();
    palette.setColor(QPalette::Base, QColor(255, 255, 255));
    palette.setColor(QPalette::Text, QColor(23, 49, 63));
    palette.setColor(QPalette::PlaceholderText, QColor(105, 121, 129));
    edit->setPalette(palette);
}

void WorkflowWindow::selectStage(beam::gui::WorkflowStage stage) {
    selectedStage_ = stage;
    // Visits and last-treated change as the session runs, so the list is rebuilt on arrival
    if (stage == beam::gui::WorkflowStage::CaseSetup) refreshPatientChooser();
    ui_->pageTitle->setText(QString::fromUtf8(beam::gui::workflowStageName(stage).data()));
    const int stageIndex = static_cast<int>(stage);
    ui_->pageStack->setCurrentIndex(stageIndex <= 3 ? stageIndex : 4);
    // A QStackedWidget takes its minimum height from its tallest page, so Registration's
    for (int i = 0; i < ui_->pageStack->count(); ++i) {
        QWidget* page = ui_->pageStack->widget(i);
        QSizePolicy policy = page->sizePolicy();
        policy.setVerticalPolicy(i == ui_->pageStack->currentIndex()
                                     ? QSizePolicy::Preferred
                                     : QSizePolicy::Ignored);
        page->setSizePolicy(policy);
    }
    // Once the page is actually up, so centring reads real geometry.
    QTimer::singleShot(0, this, [this] { centreViewsOnTarget(); });
    // Not on Treatment either: that page carries its own status line, and a second message box
    const bool showsFooter = stage != beam::gui::WorkflowStage::Registration &&
                             stage != beam::gui::WorkflowStage::TreatmentPlan;
    ui_->workflowMessage->setVisible(showsFooter);
    // The space under the stack is the footer line's.
    if (ui_->contentLayout) {
        const QMargins margins = ui_->contentLayout->contentsMargins();
        ui_->contentLayout->setContentsMargins(margins.left(), margins.top(),
                                               margins.right(), showsFooter ? 18 : 0);
    }
    ui_->completeStageButton->setVisible(stage != beam::gui::WorkflowStage::CaseSetup &&
                                         stage != beam::gui::WorkflowStage::SystemCheck &&
                                         stage != beam::gui::WorkflowStage::Imaging &&
                                         stage != beam::gui::WorkflowStage::Registration &&
                                         stage != beam::gui::WorkflowStage::Coupling &&
                                         stage != beam::gui::WorkflowStage::Correction &&
                                         stage != beam::gui::WorkflowStage::TreatmentPlan &&
                                         stage != beam::gui::WorkflowStage::SafetyReview &&
                                         stage != beam::gui::WorkflowStage::Treatment);
    const bool couplingPage = stage == beam::gui::WorkflowStage::Coupling ||
                              stage == beam::gui::WorkflowStage::Correction;
    const bool correctionPage = false;
    if (couplingPage) {
        // Acceptance disables the action buttons, but the combined Correction/Coupling page is
        runCouplingCheckButton_->setEnabled(true);
        runCorrectionButton_->setEnabled(true);
        acceptCouplingButton_->setEnabled(couplingCheckPassed_);
        acceptCorrectionButton_->setEnabled(correctionCheckPassed_);
    }
    const bool treatmentPlanPage = stage == beam::gui::WorkflowStage::TreatmentPlan;
    const bool treatmentExecutionPage = stage == beam::gui::WorkflowStage::Treatment ||
                                        stage == beam::gui::WorkflowStage::TreatmentPlan;
    const bool safetyReviewPage = stage == beam::gui::WorkflowStage::SafetyReview;
    const bool reportStage = stage == beam::gui::WorkflowStage::Report;
    // Every widget below lives on placeholderPage, and setVisible(true) activates its parent's
    if (ui_->placeholderLayout) ui_->placeholderLayout->setEnabled(false);
    if (ui_->treatmentMriPage) ui_->treatmentMriPage->setVisible(treatmentPlanPage);
    // The Treatment plan and Treatment stages each own one container, so showing a stage is one
    if (treatmentPlanBody_) treatmentPlanBody_->setVisible(treatmentPlanPage);
    if (treatmentExecutionBody_) treatmentExecutionBody_->setVisible(treatmentExecutionPage);
    // The Report stage shares placeholderPage with Coupling and Treatment, so it is shown and
    if (reportBody_ != nullptr) {
        const bool reportPage = stage == beam::gui::WorkflowStage::Report;
        reportBody_->setVisible(reportPage);
        if (reportPage) refreshReport();
    }
    if (placeholderTailSpacer_) {
        placeholderTailSpacer_->changeSize(
            0, 0, QSizePolicy::Minimum,
            (treatmentPlanPage || treatmentExecutionPage || reportStage) ? QSizePolicy::Fixed
                                                                        : QSizePolicy::Expanding);
        ui_->placeholderLayout->invalidate();
    }
    for (QWidget* widget : {static_cast<QWidget*>(couplingTitleLabel_),
                            static_cast<QWidget*>(couplingDescriptionLabel_),
                            static_cast<QWidget*>(couplingStatusLabel_),
                            static_cast<QWidget*>(couplingProgressBar_),
                            static_cast<QWidget*>(runCouplingCheckButton_),
                            static_cast<QWidget*>(clearCouplingButton_),
                            static_cast<QWidget*>(acceptCouplingButton_),
                            static_cast<QWidget*>(couplingChartsRow_),
                            static_cast<QWidget*>(couplingNumbersLabel_),
                            static_cast<QWidget*>(correctionTitleLabel_),
                            static_cast<QWidget*>(correctionDescriptionLabel_),
                            static_cast<QWidget*>(correctionStatusLabel_),
                            static_cast<QWidget*>(correctionProgressBar_),
                            static_cast<QWidget*>(runCorrectionButton_),
                            static_cast<QWidget*>(acceptCorrectionButton_),
                            static_cast<QWidget*>(safetyReviewTitleLabel_),
                            static_cast<QWidget*>(safetyReviewDescriptionLabel_),
                            static_cast<QWidget*>(safetyReviewStatusLabel_),
                            static_cast<QWidget*>(safetyRegistrationCheckBox_),
                            static_cast<QWidget*>(safetyCorrectionCheckBox_),
                            static_cast<QWidget*>(safetyPlanCheckBox_),
                            static_cast<QWidget*>(acceptSafetyReviewButton_)}) {
        if (!widget) continue;
        const bool isCorrectionWidget = widget == correctionTitleLabel_ || widget == correctionDescriptionLabel_ ||
                                        widget == correctionStatusLabel_ || widget == correctionProgressBar_ ||
                                        widget == runCorrectionButton_ || widget == acceptCorrectionButton_;
        const bool isSafetyWidget = widget == safetyReviewTitleLabel_ || widget == safetyReviewDescriptionLabel_ ||
                                    widget == safetyReviewStatusLabel_ || widget == safetyRegistrationCheckBox_ ||
                                    widget == safetyCorrectionCheckBox_ || widget == safetyPlanCheckBox_ ||
                                    widget == acceptSafetyReviewButton_;
        widget->setVisible(isSafetyWidget ? safetyReviewPage
                                          : (isCorrectionWidget ? correctionPage : couplingPage));
    }
    // The link panel shares placeholderPage with every other stage body, so it needs the same
    if (deviceLink_) deviceLink_->setVisible(couplingPage);
    // Report has a page of its own now, so it stops being "awaiting its BeamV0 behavioral audit
    if (ui_->placeholderText)
        ui_->placeholderText->setVisible(!couplingPage && !correctionPage && !treatmentPlanPage &&
                                         !treatmentExecutionPage && !safetyReviewPage &&
                                         !reportStage);
    if (ui_->placeholderLayout) {
        ui_->placeholderLayout->setEnabled(true);
        ui_->placeholderLayout->activate();
    }
    if (treatmentPlanPage) {
        // Entering the stage refreshes X/Y/Z: an unplaced target reads the array centre that
        switchToSelectedTarget();
        // Open on the registered array position -- what the operator is about to sonicate --
        if (!targetMm_.isZero()) {
            focusTreatmentViewsOn(targetMm_);
        } else {
            treatmentFiducialPick_ = 0;
            navigateToFiducialOnTreatment(0, false);
        }
        // There is no separate review: this page is where the plan is made and where it is
        std::string planned;
        workflow_.complete(beam::gui::WorkflowStage::TreatmentPlan, &planned);
        workflow_.complete(beam::gui::WorkflowStage::SafetyReview, &planned);
    }
    if (treatmentExecutionPage) {
        // Firing is available only once the plan and its safety review are both accepted -- the
        const bool ready =
            workflow_.state(beam::gui::WorkflowStage::SafetyReview).status == beam::gui::WorkflowStatus::Complete;
        setSonicateEnabled(ready && !sonicationCountdownTimer_->isActive());
        // Registration and through-transmit are two of its inputs, and both can have changed
        updateSonicationSafety();
        // The disabled Sonicate button already says the plan is not accepted;
        // a panel repeating it occupied the page for the whole session.
    }
    if (safetyReviewPage) {
        const auto complete = [this](beam::gui::WorkflowStage checkStage) {
            return workflow_.state(checkStage).status == beam::gui::WorkflowStatus::Complete;
        };
        const bool registrationOk = complete(beam::gui::WorkflowStage::Registration);
        const bool correctionOk = complete(beam::gui::WorkflowStage::Correction);
        const bool planOk = complete(beam::gui::WorkflowStage::TreatmentPlan);
        safetyRegistrationCheckBox_->setChecked(registrationOk);
        safetyCorrectionCheckBox_->setChecked(correctionOk);
        safetyPlanCheckBox_->setChecked(planOk);
        acceptSafetyReviewButton_->setEnabled(registrationOk && correctionOk && planOk);
        safetyReviewStatusLabel_->setText(registrationOk && correctionOk && planOk
            ? QStringLiteral("All safety prerequisites are complete. Review and accept to continue.")
            : QStringLiteral("One or more prerequisites are incomplete; treatment remains locked."));
    }
    if (stage == beam::gui::WorkflowStage::Registration && mriLoaded_) {
        showMriPreviews();
        const int row = ui_->registrationTable->currentRow();
        // Only the FIRST arrival jumps to a fiducial.
        if (row >= 0 && registrationStartFiducialRow_ < 0) {
            navigateToRegistrationFiducial(row);
            registrationStartFiducialRow_ = row;
            ui_->registrationReturnStartButton->setEnabled(true);
        }
    }
    if (stage != beam::gui::WorkflowStage::Registration) {
        // Fiducial markers are registration overlays.
        ui_->sagittalPreview->setMarkers({});
        ui_->coronalPreview->setMarkers({});
        ui_->axialPreview->setMarkers({});
    }
    if (stage == beam::gui::WorkflowStage::Registration) {
        if (registrationPhase_ == RegistrationPhase::Accepted) {
            // Returning to a completed Registration page is a review state;
            // do not present Step 3/4 as if they still require action.
        }
    }
    refresh();
    if (stage == beam::gui::WorkflowStage::Registration) {
        if (calibrationGroup_) calibrationGroup_->setEnabled(true);
        updateRegistrationAvailability();
    }
    QTimer::singleShot(0, this, [this] { mriHeightSyncPasses_ = 0; syncMriViewerHeights(); });
}

void WorkflowWindow::completeCurrentStage() {
    // Accept also opens the record when Create patient was not pressed, so the one-press path
    if (selectedStage_ == beam::gui::WorkflowStage::CaseSetup && !caseFieldsUsable()) return;
    std::string reason;
    if (!workflow_.complete(selectedStage_, &reason)) {
        showMessage(QString::fromStdString(reason), true);
        return;
    }
    const bool caseAccepted = selectedStage_ == beam::gui::WorkflowStage::CaseSetup;
    if (caseAccepted) {
        // Mark the optional hardware gate complete so Imaging is immediately available for
        workflow_.complete(beam::gui::WorkflowStage::SystemCheck, nullptr);
    }
    showMessage(QStringLiteral("%1 complete.").arg(QString::fromUtf8(beam::gui::workflowStageName(selectedStage_).data())), false);
    // Before nextStage() is consulted, not after.
    const bool wasUpdating = updatesEnabled();
    if (caseAccepted) setUpdatesEnabled(false);
    if (caseAccepted) selectPatient(caseDemographics(), /*advancing=*/true);
    goToNextStage();
    refresh();
    if (caseAccepted) setUpdatesEnabled(wasUpdating);
}


// Port of the array-position line BeamV0's drawROIs.m maintains: centerArrayMM =
void WorkflowWindow::updateArrayPositionReadout() {
    if (!arrayPositionLabel_) return;
    const Eigen::Index columns = arrayData_.arrayTotal.rect.cols();
    if (!registrationGeometryLoaded_ || columns == 0) {
        arrayPositionLabel_->setText(QStringLiteral("Array position    not placed"));
        return;
    }
    const Eigen::Vector3d centreMm =
        arrayData_.arrayTotal.rect.block(beam::array::kRectCenterStartRow, 0, 3, columns)
            .rowwise()
            .mean() *
        1000.0;
    arrayPositionLabel_->setText(QStringLiteral("Array position    X %1    Y %2    Z %3  mm")
                                     .arg(centreMm.x(), 10, 'f', 3)
                                     .arg(centreMm.y(), 10, 'f', 3)
                                     .arg(centreMm.z(), 10, 'f', 3));
}

void WorkflowWindow::refresh() {
    updateArrayPositionReadout();
    for (std::size_t i = 0; i < stageCount; ++i) {
        const auto stage = static_cast<beam::gui::WorkflowStage>(i);
        const auto& state = workflow_.state(stage);
        ui_->stageList->item(static_cast<int>(i))->setText(
            QStringLiteral("%1  %2\n    %3")
                .arg(statusSymbol(state.status), QString::fromUtf8(beam::gui::workflowStageName(stage).data()),
                     QString::fromUtf8(beam::gui::workflowStatusName(state.status).data())));
    }
    std::string reason;
    const bool ready = workflow_.canStartTreatment(&reason);
    updateAbortButton();
    // Whether the visit CAN be finished, not whether a sonication happened to end while this
    if (finishTreatmentButton_ != nullptr) {
        const bool running =
            sonicationCountdownTimer_ != nullptr && sonicationCountdownTimer_->isActive();
        const bool done =
            workflow_.state(beam::gui::WorkflowStage::Treatment).status ==
            beam::gui::WorkflowStatus::Complete;
        finishTreatmentButton_->setEnabled(
            !running && !done && workflow_.canBegin(beam::gui::WorkflowStage::Treatment, nullptr));
        finishTreatmentButton_->setToolTip(
            running ? QStringLiteral("A sonication is still going out.")
            : done  ? QStringLiteral("This visit is already finished.")
                    : QStringLiteral("Ends the visit and opens its case report."));
    }
    if (selectedStage_ == beam::gui::WorkflowStage::Treatment && !ready) showMessage(QString::fromStdString(reason), true);
    // Rules 310 and 320 read stage status and the through-transmit, both of which change on
    updateSonicationSafety();
}

// updateAttenuationPlots.m's two RF plots and setAvgTransmissionDataBars.m's bar pair, from the
}  // namespace beam::app
