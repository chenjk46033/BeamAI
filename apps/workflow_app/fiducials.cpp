#include "workflow_internal.hpp"

namespace beam::app {

void WorkflowWindow::placeSelectedFiducial(const Eigen::Vector3d& positionMm, int heldAxis) {
    const int row = ui_->registrationTable->currentRow();
    if (selectedStage_ != beam::gui::WorkflowStage::Registration || row < 0 || row >= 6) return;
    // A click measures only the two axes lying in the plane clicked.
    Eigen::Vector3d placed = positionMm;
    if (heldAxis >= 0 && heldAxis < 3)
        placed(heldAxis) = fiducials_[static_cast<std::size_t>(row)].position(heldAxis) * 1000.0;
    ui_->registrationTable->blockSignals(true);
    for (int axis = 0; axis < 3; ++axis)
        ui_->registrationTable->item(row, axis + 1)->setText(QString::number(placed(axis), 'f', 1));
    ui_->registrationTable->blockSignals(false);
    fiducialLocated_[static_cast<std::size_t>(row)] = true;
    fiducialConfirmed_[static_cast<std::size_t>(row)] = false;
    fiducials_[static_cast<std::size_t>(row)].position = placed / 1000.0;
    const bool hadFit = registrationFitApplied();
    setRegistrationPhase(RegistrationPhase::Locating);
    ui_->registrationTable->item(row, 4)->setText(QStringLiteral("Located"));
    showMriPreviews();
    workflow_.change(beam::gui::WorkflowStage::Registration,
                     "Fiducial measurements changed; later approvals require review.");
    showMessage(hadFit ? QStringLiteral("Fiducial located. Moving it voided the MRI fit, so the Step 2 "
                                        "lock-position sliders are disabled until you register to MRI "
                                        "fiducials again.")
                       : QStringLiteral("Fiducial located. Verify it in another plane, then confirm the "
                                        "selected fiducial."),
                hadFit);
    refresh();
    updateRegistrationAvailability();
}

// No caller; superseded by the session file.
void WorkflowWindow::saveFiducialsCsv() {
    if (fiducials_.size() < 6) {
        showMessage(QStringLiteral("Load an MRI before saving fiducials."), true);
        return;
    }
    QString stem = QFileInfo(mriPath_).completeBaseName();
    if (stem.isEmpty()) stem = QFileInfo(mriPath_).fileName();
    if (stem.isEmpty()) stem = QStringLiteral("session");
    const QString suggested =
        QDir(fiducialDirectory()).filePath(QStringLiteral("beamai_fiducials_%1.csv").arg(stem));

    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Save measured fiducials"), suggested,
        QStringLiteral("Fiducial CSV (*.csv);;All files (*)"));
    if (path.isEmpty()) return;

    const QString written = writeFiducialsCsv(path);
    if (written.isEmpty()) {
        showMessage(QStringLiteral("Could not write %1").arg(path), true);
        return;
    }
    ui_->registrationResult->setText(QStringLiteral("Six fiducials saved to %1").arg(written));
    showMessage(QStringLiteral("Fiducials saved."), false);
}

// Identifies the scan rather than the path to it: stable when the same series is reached by
QString WorkflowWindow::fiducialDirectory() const {
    const QFileInfo mri(mriPath_);
    if (!mriPath_.isEmpty()) {
        const QString beside = mri.isDir() ? mri.absoluteFilePath() : mri.absolutePath();
        if (!beside.isEmpty() && QFileInfo(beside).isWritable()) return beside;
    }
    const QString documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString fallback = QDir(documents).filePath(QStringLiteral("BeamAI/fiducials"));
    QDir().mkpath(fallback);
    return QDir(fallback).exists() ? fallback : QDir::currentPath();
}

// Compared as numbers, not text: the same scan reached through different readers lands on
bool WorkflowWindow::sameMriGeometry(const QString& recorded, const QString& current) {
    if (recorded.isEmpty()) return true;
    const QStringList left = recorded.split(QLatin1Char(','));
    const QStringList right = current.split(QLatin1Char(','));
    if (left.size() != 9 || right.size() != 9) return recorded == current;
    for (int i = 0; i < 3; ++i)
        if (left[i].trimmed() != right[i].trimmed()) return false;
    for (int i = 3; i < 9; ++i) {
        bool okLeft = false, okRight = false;
        const double a = left[i].toDouble(&okLeft);
        const double b = right[i].toDouble(&okRight);
        if (!okLeft || !okRight || std::abs(a - b) > 0.05) return false;
    }
    return true;
}

QString WorkflowWindow::currentMriGeometry() const {
    if (mriVolume_.nx <= 0 || mriAxes_.dimLR.size() == 0) return QString();
    const auto ends = [](const Eigen::VectorXd& axis) {
        return QStringLiteral("%1,%2").arg(QString::number(axis(0), 'g', 10),
                                           QString::number(axis(axis.size() - 1), 'g', 10));
    };
    return QStringLiteral("%1,%2,%3,%4,%5,%6")
        .arg(mriVolume_.nx)
        .arg(mriVolume_.ny)
        .arg(mriVolume_.nz)
        .arg(ends(mriAxes_.dimLR), ends(mriAxes_.dimAP), ends(mriAxes_.dimIS));
}

QString WorkflowWindow::writeFiducialsCsv(const QString& path) const {
    if (fiducials_.size() < 6) return QString();
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) return QString();
    QTextStream out(&file);
    out << "# Beam measured fiducials, millimetres, RAS (+x right, +y anterior, +z superior)\n";
    out << "# MRI: " << mriPath_ << "\n";
    out << "# geometry: " << currentMriGeometry() << "\n";
    out << "# lock left: horizontal=" << leftHorizontalPositionSlider_->value()
        << ", vertical=" << leftVerticalPositionSlider_->value() << "\n";
    out << "# lock right: horizontal=" << rightHorizontalPositionSlider_->value()
        << ", vertical=" << rightVerticalPositionSlider_->value() << "\n";
    out << "name,x,y,z\n";
    for (const auto& marker : fiducials_) {
        const Eigen::Vector3d mm = marker.position * 1000.0;
        // 17 significant digits round-trips an IEEE double exactly.
        out << QString::fromStdString(marker.name) << ',' << QString::number(mm.x(), 'g', 17) << ','
            << QString::number(mm.y(), 'g', 17) << ',' << QString::number(mm.z(), 'g', 17) << '\n';
    }
    out.flush();
    file.close();
    return path;
}

void WorkflowWindow::confirmSelectedFiducial() {
    int confirmedCount = 0;
    for (int row = 0; row < 6; ++row) {
        const std::size_t index = static_cast<std::size_t>(row);
        if (!fiducialLocated_[index] || fiducialConfirmed_[index]) continue;
        fiducialConfirmed_[index] = true;
        if (ui_->registrationTable->item(row, 4))
            ui_->registrationTable->item(row, 4)->setText(QStringLiteral("Confirmed"));
        ++confirmedCount;
    }
    if (confirmedCount == 0) {
        showMessage(QStringLiteral("Locate at least one fiducial before confirming."), true);
        return;
    }
    setRegistrationPhase(RegistrationPhase::Locating);
    workflow_.change(beam::gui::WorkflowStage::Registration,
                     "Fiducial confirmation changed; registration must be recalculated.");
    updateRegistrationAvailability();

    showMessage(confirmedCount == 1
                    ? QStringLiteral("Fiducial confirmed.")
                    : QStringLiteral("%1 located fiducials confirmed.").arg(confirmedCount), false);
    ui_->registrationResult->setText(
        QStringLiteral("%1 fiducial%2 confirmed. Save... keeps them beside the MRI.")
            .arg(confirmedCount)
            .arg(confirmedCount == 1 ? QString() : QStringLiteral("s")));
    refresh();
    QTimer::singleShot(0, this, [this] { mriHeightSyncPasses_ = 0; syncMriViewerHeights(); });
}


// Matches by marker name, not row order; refuses a file missing any of the six.
void WorkflowWindow::highlightRegistrationRow(int currentRow) {
    // setBackground() reports as a cell change like any other, and the cellChanged handler
    const QSignalBlocker quiet(ui_->registrationTable);
    const QColor selectedFill(190, 235, 245);
    const QColor rejectFill(0xff, 0xe3, 0xe3);
    const QColor cautionFill(0xff, 0xf4, 0xe0);
    for (int row = 0; row < ui_->registrationTable->rowCount(); ++row) {
        const double confidence = row < 6 ? fiducialConfidence_[static_cast<std::size_t>(row)] : 0.0;
        const FiducialTrust trust = fiducialTrustOf(confidence);
        const QColor fill = row == currentRow                    ? selectedFill
                            : trust == FiducialTrust::Reject     ? rejectFill
                            : trust == FiducialTrust::Caution    ? cautionFill
                                                                 : QColor(Qt::transparent);
        for (int column = 0; column < ui_->registrationTable->columnCount(); ++column) {
            if (auto* item = ui_->registrationTable->item(row, column)) item->setBackground(fill);
        }
    }
}

// Confidence column plus per-row warning text.
void WorkflowWindow::applyFiducialConfidenceToTable() {
    const QColor rejectText(0xb1, 0x1c, 0x1c);
    const QColor cautionText(0x8a, 0x55, 0x00);
    ui_->registrationTable->blockSignals(true);
    for (int row = 0; row < 6; ++row) {
        const double confidence = fiducialConfidence_[static_cast<std::size_t>(row)];
        const double stability = fiducialStability_[static_cast<std::size_t>(row)];
        const FiducialTrust trust = fiducialTrustOf(confidence);
        if (QTableWidgetItem* cell = ui_->registrationTable->item(row, 6)) {
            if (trust == FiducialTrust::Unscored) {
                cell->setText(QStringLiteral("—"));
                cell->setToolTip(QString());
            } else {
                // Only a rejected score gets a cross; a caution-band point is often a good
                const QString mark = trust == FiducialTrust::Reject    ? QStringLiteral("  ✖")
                                     : trust == FiducialTrust::Caution ? QStringLiteral("  ?")
                                                                       : QString();
                cell->setText(QStringLiteral("%1%%2").arg(confidence * 100.0, 0, 'f', 0).arg(mark));
                QString advice;
                switch (trust) {
                    case FiducialTrust::Reject:
                        advice = QStringLiteral("Below 50% — most likely not on the marker. Place "
                                                "this one by hand.");
                        break;
                    case FiducialTrust::Caution:
                        advice = QStringLiteral("Between 50% and 70% — unverified, not necessarily "
                                                "wrong. Check it in all three planes.");
                        break;
                    default:
                        advice = QStringLiteral("Still review it before confirming.");
                        break;
                }
                cell->setToolTip(
                    QStringLiteral("Peak annulus correlation %1; neighbouring slices agree on the "
                                   "centre to %2.\n%3")
                        .arg(confidence, 0, 'f', 3)
                        .arg(stability, 0, 'f', 2)
                        .arg(advice));
            }
        }
        for (int column = 0; column < ui_->registrationTable->columnCount(); ++column) {
            QTableWidgetItem* item = ui_->registrationTable->item(row, column);
            if (item == nullptr) continue;
            if (trust == FiducialTrust::Reject || trust == FiducialTrust::Caution) {
                item->setForeground(trust == FiducialTrust::Reject ? rejectText : cautionText);
                QFont font = item->font();
                font.setBold(true);
                item->setFont(font);
            } else {
                item->setData(Qt::ForegroundRole, QVariant());
                item->setData(Qt::FontRole, QVariant());
            }
        }
    }
    ui_->registrationTable->blockSignals(false);
    highlightRegistrationRow(ui_->registrationTable->currentRow());
}

// Refines around where the point already is, so the operator decides which marker is meant.
void WorkflowWindow::detectSingleFiducial(int row) {
    if (!registrationGeometryLoaded_ || !mriLoaded_ || row < 0 || row >= 6 ||
        fiducials_.size() < 6 || registrationSourceFiducials_.size() < 6) {
        QMessageBox::warning(this, QStringLiteral("Detect fiducial"),
                             QStringLiteral("Load an MRI and its transducer geometry first."));
        return;
    }
    const std::size_t index = static_cast<std::size_t>(row);
    const QString name = QString::fromStdString(fiducials_[index].name);
    const Eigen::Vector3d beforeMm = fiducials_[index].position * 1000.0;

    // The same search the Detect fiducials button runs, from the nominal prior; only this row's
    std::vector<Eigen::Vector3d> priorMm;
    std::vector<std::string> names;
    for (const auto& marker : registrationSourceFiducials_) {
        priorMm.push_back(marker.position * 1000.0);
        names.push_back(marker.name);
    }

    beam::mri::FiducialDetection found;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    try {
        const std::vector<beam::mri::FiducialDetection> all =
            beam::mri::detectFiducialDonuts(mriVolume_, mriAxes_, priorMm, names);
        found = all[index];
        if (!found.found) {
            std::vector<Eigen::Vector3d> avoidMm;
            for (std::size_t other = 0; other < fiducials_.size(); ++other)
                if (other != index) avoidMm.push_back(fiducials_[other].position * 1000.0);
            const std::vector<beam::mri::FiducialDetection> ranked =
                beam::mri::detectFiducialDonutsNear(mriVolume_, mriAxes_, beforeMm, avoidMm,
                                                    kSingleFiducialSearchRadiusMm, names[index]);
            if (!ranked.empty()) found = ranked.front();
        }
    } catch (const std::exception& error) {
        QApplication::restoreOverrideCursor();
        QMessageBox::warning(this, QStringLiteral("Detect fiducial"),
                             QStringLiteral("Detection failed: %1").arg(error.what()));
        return;
    }
    QApplication::restoreOverrideCursor();

    if (!found.found) {
        const QString message = QStringLiteral("%1 was not found. Place it by hand.").arg(name);
        ui_->registrationResult->setText(message);
        showMessage(message, true);
        QMessageBox::information(this, QStringLiteral("Detect fiducial"), message);
        return;
    }

    const double movedMm = (found.positionMm - beforeMm).norm();
    fiducials_[index].position = found.positionMm / 1000.0;
    fiducialConfidence_[index] = found.confidence;
    fiducialStability_[index] = found.stability;
    fiducialLocated_[index] = true;
    fiducialConfirmed_[index] = false;
    sourceFiducialConfirmed_[index] = false;
    fiducialDetected_[index] = true;

    ui_->registrationTable->blockSignals(true);
    for (int axis = 0; axis < 3; ++axis) {
        if (QTableWidgetItem* cell = ui_->registrationTable->item(row, axis + 1))
            cell->setText(QString::number(found.positionMm(axis), 'f', 1));
    }
    if (QTableWidgetItem* cell = ui_->registrationTable->item(row, 4))
        cell->setText(QStringLiteral("Detected"));
    ui_->registrationTable->blockSignals(false);
    applyFiducialConfidenceToTable();

    // Moving one point invalidates any fit from the old six.
    setRegistrationPhase(RegistrationPhase::Locating);
    workflow_.change(beam::gui::WorkflowStage::Registration,
                     "A fiducial was re-detected; registration must be recalculated.");
    showMriPreviews();
    updateRegistrationAvailability();
    ui_->registrationTable->setCurrentCell(row, 0);
    ui_->registrationTable->selectRow(row);
    ui_->registrationFiducialLayout->setSelectedIndex(row);
    navigateToRegistrationFiducial(row);

    const QString message = QStringLiteral("%1 re-detected at %2%% confidence, moved %3 mm.")
                                .arg(name)
                                .arg(found.confidence * 100.0, 0, 'f', 0)
                                .arg(movedMm, 0, 'f', 2);
    ui_->registrationResult->setText(message);
    showMessage(message, fiducialTrustOf(found.confidence) != FiducialTrust::Trusted);
    refresh();
}

void WorkflowWindow::detectFiducials() {
    if (!registrationGeometryLoaded_ || !mriLoaded_ || fiducials_.size() < 6 ||
        registrationSourceFiducials_.size() < 6) {
        showMessage(QStringLiteral("Load an MRI before detecting fiducials."), true);
        return;
    }

    // Nominal placement, never the current edits: seeding from an edited point would only
    std::vector<Eigen::Vector3d> priorMm;
    std::vector<std::string> names;
    for (const auto& marker : registrationSourceFiducials_) {
        priorMm.push_back(marker.position * 1000.0);
        names.push_back(marker.name);
    }

    std::vector<beam::mri::FiducialDetection> detections;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    detectFiducialsButton_->setEnabled(false);
    if (fiducialActionsButton_) fiducialActionsButton_->setEnabled(false);
    try {
        // The rejected and suppressed peaks are deliberately not requested: the views show only
        detections = beam::mri::detectFiducialDonuts(mriVolume_, mriAxes_, priorMm, names);
    } catch (const std::exception& error) {
        detectFiducialsButton_->setEnabled(true);
        if (fiducialActionsButton_) fiducialActionsButton_->setEnabled(true);
        QApplication::restoreOverrideCursor();
        showMessage(QStringLiteral("Fiducial detection failed: %1").arg(error.what()), true);
        return;
    }
    detectFiducialsButton_->setEnabled(true);
    if (fiducialActionsButton_) fiducialActionsButton_->setEnabled(true);
    QApplication::restoreOverrideCursor();
    int detected = 0;
    int rejected = 0;
    int caution = 0;
    ui_->registrationTable->blockSignals(true);
    for (int row = 0; row < 6; ++row) {
        const std::size_t index = static_cast<std::size_t>(row);
        const beam::mri::FiducialDetection& found = detections[index];
        fiducialConfidence_[index] = found.found ? found.confidence : 0.0;
        fiducialStability_[index] = found.found ? found.stability : 0.0;
        if (!found.found) continue;
        ++detected;
        if (fiducialTrustOf(found.confidence) == FiducialTrust::Reject) ++rejected;
        if (fiducialTrustOf(found.confidence) == FiducialTrust::Caution) ++caution;
        fiducials_[index].position = found.positionMm / 1000.0;
        for (int axis = 0; axis < 3; ++axis) {
            if (QTableWidgetItem* cell = ui_->registrationTable->item(row, axis + 1))
                cell->setText(QString::number(found.positionMm(axis), 'f', 1));
        }
        // Located, deliberately not confirmed: Step 2 must still review it.
        fiducialLocated_[index] = true;
        fiducialConfirmed_[index] = false;
        sourceFiducialConfirmed_[index] = false;
        fiducialDetected_[index] = true;
        if (QTableWidgetItem* cell = ui_->registrationTable->item(row, 4))
            cell->setText(QStringLiteral("Detected"));
    }
    ui_->registrationTable->blockSignals(false);
    applyFiducialConfidenceToTable();

    if (detected == 0) {
        showMessage(QStringLiteral("No marker donuts were found. Locate the six fiducials by hand."),
                    true);
        ui_->registrationResult->setText(
            QStringLiteral("Automatic detection found nothing. Locate the six fiducials by hand."));
        return;
    }

    setRegistrationPhase(RegistrationPhase::Locating);
    workflow_.change(beam::gui::WorkflowStage::Registration,
                     "Fiducials detected automatically; each must be confirmed.");
    showMriPreviews();
    updateRegistrationAvailability();

    // Always marker 1, never the first marker to clear the threshold: that let the confidences
    constexpr int openOn = 0;
    ui_->registrationTable->setCurrentCell(openOn, 0);
    ui_->registrationTable->selectRow(openOn);
    ui_->registrationFiducialLayout->setSelectedIndex(openOn);
    // setCurrentCell only navigates via currentCellChanged when the row actually changes, and
    navigateToRegistrationFiducial(openOn);

    QString summary = QStringLiteral("Detected %1 of 6 fiducials.").arg(detected);
    if (rejected > 0)
        summary += QStringLiteral(" %1 below 50%% confidence, marked in red — place those by hand.")
                       .arg(rejected);
    if (caution > 0)
        summary += QStringLiteral(" %1 between 50%% and 70%%, marked in amber — unverified, check "
                                  "in all three planes.")
                       .arg(caution);
    if (rejected == 0 && caution == 0)
        summary += QStringLiteral(" Confirm each one after checking it in all three planes.");
    ui_->registrationResult->setText(summary);
    showMessage(summary, rejected > 0 || caution > 0);
    refresh();
}

// No caller; superseded by the session file.
void WorkflowWindow::importFiducialsCsv() {
    if (!registrationGeometryLoaded_ || fiducials_.size() < 6) {
        showMessage(QStringLiteral("Load an MRI before importing fiducials."), true);
        return;
    }
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Import measured fiducials"),
        fiducialDirectory(), QStringLiteral("Fiducial CSV (*.csv);;All files (*)"));
    if (path.isEmpty()) return;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        showMessage(QStringLiteral("Could not open %1").arg(path), true);
        return;
    }
    QHash<QString, Eigen::Vector3d> byName;
    QString sourceMri;
    QString sourceGeometry;
    int lockLeftHorizontal = -1;
    int lockLeftVertical = -1;
    int lockRightHorizontal = -1;
    int lockRightVertical = -1;
    QTextStream in(&file);
    int lineNumber = 0;
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        ++lineNumber;
        // Measured against a different scan registers silently against the wrong anatomy; warn,
        if (line.startsWith(QStringLiteral("# MRI:"))) {
            sourceMri = line.mid(6).trimmed();
            continue;
        }
        if (line.startsWith(QStringLiteral("# geometry:"))) {
            sourceGeometry = line.mid(11).trimmed();
            continue;
        }
        if (line.startsWith(QStringLiteral("# lock"))) {
            static const QRegularExpression pattern(
                QStringLiteral("^# lock(?: (left|right))?: *horizontal=(\\d+), *vertical=(\\d+)"));
            const QRegularExpressionMatch found = pattern.match(line);
            if (found.hasMatch()) {
                const QString side = found.captured(1);
                const int horizontal = found.captured(2).toInt();
                const int vertical = found.captured(3).toInt();
                // A pre-two-sided file records one pair; it was written only when both sides
                if (side != QStringLiteral("right")) {
                    lockLeftHorizontal = horizontal;
                    lockLeftVertical = vertical;
                }
                if (side != QStringLiteral("left")) {
                    lockRightHorizontal = horizontal;
                    lockRightVertical = vertical;
                }
            }
            continue;
        }
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')) ||
            line.startsWith(QStringLiteral("name,")))
            continue;
        const QStringList parts = line.split(QLatin1Char(','));
        if (parts.size() != 4) {
            showMessage(QStringLiteral("%1 line %2: expected name,x,y,z").arg(path).arg(lineNumber), true);
            return;
        }
        bool okX = false, okY = false, okZ = false;
        const Eigen::Vector3d mm(parts[1].trimmed().toDouble(&okX),
                                 parts[2].trimmed().toDouble(&okY),
                                 parts[3].trimmed().toDouble(&okZ));
        if (!okX || !okY || !okZ || !mm.allFinite()) {
            showMessage(QStringLiteral("%1 line %2: coordinate is not a finite number")
                            .arg(path).arg(lineNumber), true);
            return;
        }
        byName.insert(parts[0].trimmed(), mm);
    }

    for (const auto& marker : fiducials_) {
        const QString name = QString::fromStdString(marker.name);
        if (!byName.contains(name)) {
            showMessage(QStringLiteral("%1 has no row for fiducial \"%2\"; nothing imported.")
                            .arg(path, name), true);
            return;
        }
    }

    ui_->registrationTable->blockSignals(true);
    for (int row = 0; row < 6; ++row) {
        const std::size_t index = static_cast<std::size_t>(row);
        const Eigen::Vector3d mm = byName.value(QString::fromStdString(fiducials_[index].name));
        fiducials_[index].position = mm / 1000.0;
        for (int axis = 0; axis < 3; ++axis)
            ui_->registrationTable->item(row, axis + 1)->setText(QString::number(mm(axis), 'f', 1));
        // Already confirmed once, so Step 2 is available immediately.
        fiducialLocated_[index] = true;
        fiducialConfirmed_[index] = true;
        // An imported point has no detector score; an old one is stale.
        fiducialConfidence_[index] = 0.0;
        fiducialStability_[index] = 0.0;
        ui_->registrationTable->item(row, 4)->setText(QStringLiteral("Confirmed"));
    }
    ui_->registrationTable->blockSignals(false);
    applyFiducialConfidenceToTable();

    const std::array<std::pair<QSlider*, int>, 4> restored{{
        {leftHorizontalPositionSlider_, lockLeftHorizontal},
        {leftVerticalPositionSlider_, lockLeftVertical},
        {rightHorizontalPositionSlider_, lockRightHorizontal},
        {rightVerticalPositionSlider_, lockRightVertical},
    }};
    for (const auto& [slider, value] : restored)
        if (value > 0) slider->setValue(value);

    setRegistrationPhase(RegistrationPhase::Locating);
    workflow_.change(beam::gui::WorkflowStage::Registration,
                     "Fiducials imported; registration must be recalculated.");
    showMriPreviews();
    updateRegistrationAvailability();
    const bool mriMatches = sameMriGeometry(sourceGeometry, currentMriGeometry());
    if (mriMatches) {
        ui_->registrationResult->setText(
            QStringLiteral("Imported 6 fiducials from %1. Run Register to MRI fiducials.").arg(path));
        showMessage(QStringLiteral("Imported 6 fiducials."), false);
    } else {
        ui_->registrationResult->setText(
            QStringLiteral("Imported 6 fiducials from %1, but they were measured on a different "
                           "MRI (%2) from the one now loaded. Check the subject before registering.")
                .arg(path, sourceMri));
        showMessage(QStringLiteral("Imported 6 fiducials measured on a DIFFERENT MRI (%1). Verify the "
                                   "subject before registering.").arg(sourceMri), true);
    }
    refresh();
    QTimer::singleShot(0, this, [this] { mriHeightSyncPasses_ = 0; syncMriViewerHeights(); });
}

}  // namespace beam::app
