#include "treatment_internal.hpp"

namespace beam::app {

void WorkflowWindow::buildTreatmentExecutionBody(const QString& acceptButtonStyle) {
    treatmentExecutionBody_ = new QWidget(ui_->placeholderPage);
    // Only as tall as the one status line it holds.
    treatmentExecutionBody_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto* outer = new QVBoxLayout(treatmentExecutionBody_);
    outer->setContentsMargins(0, 0, 0, 0);

    // No heading or blurb: execution is part of this page now, not a stage of its own, and the

    auto* row = new QHBoxLayout;

    // One line, not three stacked: the countdown reads at the left, the progress bar takes the
    auto* statusColumn = new QHBoxLayout;
    statusColumn->setSpacing(8);
    sonicationCountdownLabel_ = new QLabel(QStringLiteral("No sonication running."), treatmentExecutionBody_);
    sonicationCountdownLabel_->setStyleSheet(QStringLiteral("QLabel { color: #dcebef; padding: 4px; }"));
    treatmentExecutionProgressBar_ = new QProgressBar(treatmentExecutionBody_);
    treatmentExecutionProgressBar_->setRange(0, 100);
    treatmentExecutionProgressBar_->setValue(0);
    treatmentExecutionProgressBar_->setTextVisible(false);
    // Only while something is running: a bar left on the page with nothing moving reads as a
    treatmentExecutionProgressBar_->setVisible(false);
    treatmentExecutionProgressBar_->setMinimumHeight(24);
    treatmentExecutionProgressBar_->setMinimumWidth(210);
    treatmentExecutionProgressBar_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    // The bar takes the larger share of the row: it is what the operator watches while a
    sonicationSafetyLabel_->setParent(treatmentExecutionBody_);
    statusColumn->addWidget(sonicationCountdownLabel_, 0);
    statusColumn->addWidget(sonicationSafetyLabel_, 1);
    statusColumn->addWidget(treatmentExecutionProgressBar_, 1);
    row->addLayout(statusColumn, 1);

    // Right: the .mlapp's own narrow control column.
    auto* controlColumn = new QVBoxLayout;
    // Tight, so the whole column clears the body that holds it: at the default spacing Abort
    controlColumn->setSpacing(4);
    controlColumn->setContentsMargins(0, 0, 0, 0);
    auto* controlPanel = new QWidget(treatmentExecutionBody_);
    // The same width as the Target List on the other side, so the page reads as a table between
    controlPanel->setMinimumWidth(140);
    controlPanel->setMaximumWidth(140);
    controlPanel->setLayout(controlColumn);
    const auto columnLabel = [&](const QString& text) {
        auto* label = new QLabel(text, controlPanel);
        label->setStyleSheet(QStringLiteral("QLabel { color: #dcebef; }"));
        return label;
    };
    const auto makeDeliveryButton = [this, controlPanel](const QString& text, bool sham) {
        auto* button = new QPushButton(text, controlPanel);
        button->setIcon(sham ? makeShamIcon(18, kInkOnYellow) : makeSonicateIcon(18, kInkOnBlue));
        button->setIconSize(QSize(18, 18));
        button->setMinimumHeight(36);
        button->setCursor(Qt::PointingHandCursor);
        button->setStyleSheet(QString::fromLatin1(sham ? kShamStyle : kSonicateStyle));
        connect(button, &QPushButton::clicked, this, [this, sham] { runSonication(sham); });
        (sham ? inRowShamButton_ : inRowSonicateButton_) = button;
        return button;
    };
    triggerModeCombo_ = new QComboBox(controlPanel);
    triggerModeCombo_->addItems({QStringLiteral("Immediate"), QStringLiteral("External")});

    // A plan holds many sonications and BeamV0 fires them one at a time, so finishing the stage
    finishTreatmentButton_ = new QPushButton(QStringLiteral("Finish Treatment"),
                                             treatmentExecutionBody_);
    finishTreatmentButton_->setMinimumHeight(40);
    finishTreatmentButton_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    finishTreatmentButton_->setEnabled(false);

    // The device link lives on the Coupling page: it has to be up before the correction
    controlColumn->addWidget(columnLabel(QStringLiteral("Trigger:")));
    controlColumn->addWidget(triggerModeCombo_);
    // The two deliveries, as words.
    controlColumn->addSpacing(20);
    controlColumn->addWidget(makeDeliveryButton(QStringLiteral("Sonicate"), false));
    controlColumn->addSpacing(30);
    controlColumn->addWidget(makeDeliveryButton(QStringLiteral("Sham"), true));
    controlColumn->addSpacing(6);
    // Slack below, or the box layout spreads the controls apart.

    // Beside the plan rather than under it.
    controlPanel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    if (treatmentBodyRow_) {
        controlPanel->setParent(nullptr);
        treatmentBodyRow_->addWidget(controlPanel, 0, Qt::AlignTop);
    } else {
        row->addWidget(controlPanel, 0, Qt::AlignTop);
    }
    // No stretch: this row is one line of status and a button.
    finishTreatmentButton_->setStyleSheet(QStringLiteral(
        "QPushButton { background: #176b87; color: #ffffff; border: 1px solid #0f5269; "
        "border-radius: 5px; font-weight: 700; letter-spacing: 0.5px; } "
        "QPushButton:hover { background: #2083a3; } "
        "QPushButton:pressed { background: #0f5269; } "
        "QPushButton:disabled { background: #3b4a52; color: #93a3ab; border-color: #2b373d; }"));
    outer->addWidget(finishTreatmentButton_);
    // The countdown and its bar read below the button, so the last thing on the page is what is
    outer->addLayout(row);
    // Its own height, held: the body is Fixed, but a page too short for everything above it
    treatmentExecutionBody_->setMinimumHeight(outer->sizeHint().height());
    treatmentExecutionBody_->setVisible(false);

    // Not startStandaloneCountdown.m's 2 s tick arithmetic: that runs two ticks past zero, so a
    sonicationCountdownTimer_ = new QTimer(this);
    sonicationCountdownTimer_->setInterval(100);
    connect(sonicationCountdownTimer_, &QTimer::timeout, this, [this] {
        const qint64 remainingMs = sonicationDeadlineMs_ - QDateTime::currentMSecsSinceEpoch();
        const int left = static_cast<int>((remainingMs + 999) / 1000);
        if (remainingMs <= 0) {
            sonicationCountdownTimer_->stop();
            if (runningFlashTimer_ != nullptr) runningFlashTimer_->stop();
            sonicationCountdownLabel_->setText(QStringLiteral("No sonication running."));
            closeOpenLogRow();
            stopShamAudio();
            // Off the page rather than sitting at 100%: the Status column now says the
            treatmentExecutionProgressBar_->setValue(0);
            treatmentExecutionProgressBar_->setVisible(false);
            setSonicateEnabled(true);
            finishTreatmentButton_->setEnabled(true);
            if (deviceLink_ != nullptr) deviceLink_->setDelivering(false);
            return;
        }
        sonicationCountdownLabel_->setText(
            QString::fromStdString(beam::gui::countdownDisplayText(std::max(left, 0))));
        if (sonicationCountdownSeconds_ > 0) {
            const int done = sonicationCountdownSeconds_ - left;
            treatmentExecutionProgressBar_->setValue(
                std::clamp(done * 100 / sonicationCountdownSeconds_, 0, 100));
        }
    });

    connect(finishTreatmentButton_, &QPushButton::clicked, this, [this] { finishTreatmentStage(); });
}

// generalSonicateMaster.m's decision path: build stimParams from the shown row, run the two

QString WorkflowWindow::visitStatePath() const {
    const QString directory = patientDataDirectory();
    if (directory.isEmpty()) return QString();
    return QDir(directory).filePath(QStringLiteral("treatment_visit_state.csv"));
}

// Where each visit was left.
void WorkflowWindow::saveVisitState() const {
    const QString path = visitStatePath();
    // Nobody named, so nothing to file it against.
    if (path.isEmpty()) return;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) return;
    QTextStream out(&file);
    out << "Protocol,Visit,CurrentSonication,Edited,AutoRank\n";
    // Every visit any of the three knows about: one created and left empty has no position of
    QSet<QString> keys;
    for (auto entry = visitSonication_.constBegin(); entry != visitSonication_.constEnd();
         ++entry)
        keys.insert(entry.key());
    keys.unite(editedVisits_);
    keys.unite(rankingOffVisits_);
    QStringList ordered(keys.constBegin(), keys.constEnd());
    ordered.sort();  // a stable file, so a diff of it means something
    for (const QString& key : ordered) {
        const int split = key.lastIndexOf(QLatin1Char('|'));
        if (split <= 0) continue;
        QString protocol = key.left(split);
        protocol.replace(QLatin1Char('"'), QStringLiteral("\"\""));
        out << '"' << protocol << "\"," << key.mid(split + 1) << ','
            << visitSonication_.value(key, 1) << ',' << (editedVisits_.contains(key) ? 1 : 0)
            << ',' << (rankingOffVisits_.contains(key) ? 0 : 1) << '\n';
    }
}

// The operator has changed this visit's rows, so the schedule no longer describes it and must
void WorkflowWindow::markVisitEdited(const QString& protocol, int visit) {
    editedVisits_.insert(visitKey(protocol, visit));
    saveVisitState();
}

void WorkflowWindow::loadVisitState() {
    const QString path = visitStatePath();
    if (path.isEmpty()) return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    QTextStream in(&file);
    in.readLine();  // header
    while (!in.atEnd()) {
        const QString line = in.readLine();
        if (line.trimmed().isEmpty()) continue;
        // "Protocol",Visit,CurrentSonication[,Edited[,AutoRank]] -- the protocol is quoted
        if (!line.startsWith(QLatin1Char('"'))) continue;
        QString protocol;
        int at = 1;
        bool closed = false;
        while (at < line.size()) {
            if (line.at(at) == QLatin1Char('"')) {
                if (at + 1 < line.size() && line.at(at + 1) == QLatin1Char('"')) {
                    protocol += QLatin1Char('"');
                    at += 2;
                    continue;
                }
                closed = true;
                ++at;
                break;
            }
            protocol += line.at(at);
            ++at;
        }
        if (!closed || at >= line.size() || line.at(at) != QLatin1Char(',')) continue;
        const QStringList fields = line.mid(at + 1).split(QLatin1Char(','));
        if (fields.size() < 2) continue;
        bool visitOk = false;
        bool positionOk = false;
        const int visit = fields.at(0).trimmed().toInt(&visitOk);
        const int position = fields.at(1).trimmed().toInt(&positionOk);
        if (!visitOk || !positionOk || visit <= 0 || position <= 0) continue;
        const QString key = visitKey(protocol, visit);
        visitSonication_.insert(key, position);
        if (fields.value(2).trimmed() == QLatin1String("1")) editedVisits_.insert(key);
        // Absent means on, so a file written before this column keeps BeamV0's always-on
        if (fields.value(3).trimmed() == QLatin1String("0")) rankingOffVisits_.insert(key);
    }
}

// One key per visit of a protocol: visits are numbered per protocol, so the same number means
QString WorkflowWindow::visitKey(const QString& protocol, int visit) {
    return QStringLiteral("%1|%2").arg(protocol).arg(visit);
}

// Only the visits that exist.
int WorkflowWindow::visitForProtocol(const std::string& protocol) const {
    int latest = 0;
    for (const beam::gui::TreatmentLogRow& row : treatmentLog_.rows())
        if (row.protocol == protocol) latest = std::max(latest, row.visit);
    // An empty visit is still a visit.
    const QString prefix = QString::fromStdString(protocol) + QLatin1Char('|');
    for (const QString& key : editedVisits_) {
        if (!key.startsWith(prefix)) continue;
        bool ok = false;
        const int visit = key.mid(prefix.size()).toInt(&ok);
        if (ok) latest = std::max(latest, visit);
    }
    return latest > 0 ? latest : 1;
}

// The list is the visit history: every visit that exists, what state it is in, and one item
void WorkflowWindow::refreshVisitCombo() {
    if (!visitNumberCombo_) return;
    const std::string protocol = currentProtocolName().toStdString();
    std::set<int> existing;
    for (const beam::gui::TreatmentLogRow& row : treatmentLog_.rows())
        if (row.protocol == protocol) existing.insert(row.visit);
    // A visit emptied by hand has no rows to be found by, and dropping it from the list would
    const QString prefix = currentProtocolName() + QLatin1Char('|');
    for (const QString& key : editedVisits_) {
        if (!key.startsWith(prefix)) continue;
        bool ok = false;
        const int visit = key.mid(prefix.size()).toInt(&ok);
        if (ok && visit > 0) existing.insert(visit);
    }
    const int current = currentVisitNumber();

    const QSignalBlocker blocker(visitNumberCombo_);
    visitNumberCombo_->clear();
    for (const int visit : existing) {
        const std::vector<beam::gui::TreatmentLogRow> rows =
            treatmentLog_.rowsFor(protocol, visit);
        int delivered = 0;
        for (const beam::gui::TreatmentLogRow& row : rows)
            if (beam::gui::TreatmentLog::delivered(row)) ++delivered;
        // Nothing in it yet is simply "Visit 3": saying it is empty tells the operator what the
        const QString state =
            rows.empty() ? QString()
            : delivered == 0
                ? QStringLiteral(" -- %1 planned").arg(rows.size())
                : QStringLiteral(" -- %1 of %2 delivered").arg(delivered).arg(rows.size());
        visitNumberCombo_->addItem(QStringLiteral("Visit %1%2").arg(visit).arg(state), visit);
    }
    visitNumberCombo_->addItem(QStringLiteral("Create New Visit"), kNewVisitItem);
    const int index = visitNumberCombo_->findData(current);
    visitNumberCombo_->setCurrentIndex(index >= 0 ? index : 0);
}

int WorkflowWindow::currentVisitNumber() const {
    if (visitNumberCombo_ == nullptr) return 1;
    bool ok = false;
    const int visit = visitNumberCombo_->currentData().toInt(&ok);
    return ok && visit > 0 ? visit : 1;
}

// A new visit is empty.
void WorkflowWindow::startNewVisit() {
    const QString protocolName = currentProtocolName();
    const std::string protocol = protocolName.toStdString();
    int next = 0;
    for (int visit = 1; visit <= kMaxTreatmentVisits && next == 0; ++visit)
        if (treatmentLog_.rowsFor(protocol, visit).empty() &&
            !editedVisits_.contains(visitKey(protocolName, visit)))
            next = visit;
    if (next == 0) {
        showMessage(QStringLiteral("All %1 visits of this protocol have been used.")
                        .arg(kMaxTreatmentVisits),
                    true);
        refreshVisitCombo();  // put the list back on the visit it was showing
        return;
    }
    // Known from here on, so it keeps its place in the list even while empty.
    markVisitEdited(protocolName, next);
    selectVisit(next);
    loadTreatmentProtocol(protocolName);
}

bool WorkflowWindow::visitExists(const QString& protocol, int visit) const {
    return !treatmentLog_.rowsFor(protocol.toStdString(), visit).empty() ||
           editedVisits_.contains(visitKey(protocol, visit));
}

// A visit that has delivered nothing is a plan, not a record, and a plan can be thrown away
void WorkflowWindow::deleteVisit(int visit) {
    const QString protocolName = currentProtocolName();
    const std::string protocol = protocolName.toStdString();
    const int rows = static_cast<int>(treatmentLog_.rowsFor(protocol, visit).size());
    if (!treatmentLog_.canRemoveVisit(protocol, visit)) {
        showMessage(QStringLiteral("Visit %1 has delivered sonications and cannot be deleted.")
                        .arg(visit),
                    true);
        return;
    }
    // Nothing here can be undone, and the whole visit goes at once.
    const QString question =
        rows == 0 ? QStringLiteral("Delete visit %1?").arg(visit)
                  : QStringLiteral("Delete visit %1 and its %2 planned sonication(s)?")
                        .arg(visit)
                        .arg(rows);
    if (QMessageBox::question(this, QStringLiteral("Delete visit"), question) !=
        QMessageBox::Yes)
        return;

    treatmentLog_.removeVisit(protocol, visit);
    const QString key = visitKey(protocolName, visit);
    editedVisits_.remove(key);
    visitSonication_.remove(key);
    saveVisitState();
    refreshVisitCombo();
    // Whatever is left of this protocol, or nothing -- and then the list is back to its create
    selectVisit(visitForProtocol(protocol));
    loadTreatmentProtocol(protocolName);
    // Reload first: saving writes the table back into the log, and the table still held the
    saveTreatmentTargetStore();
    showMessage(QStringLiteral("Visit %1 deleted.").arg(visit), false);
}

void WorkflowWindow::selectVisit(int visit) {
    if (visitNumberCombo_ == nullptr) return;
    const QSignalBlocker blocker(visitNumberCombo_);
    int index = visitNumberCombo_->findData(visit);
    // A visit just created is not in the list yet -- the rebuild that follows puts it in its
    if (index < 0 && visitExists(currentProtocolName(), visit)) {
        visitNumberCombo_->insertItem(0, QStringLiteral("Visit %1").arg(visit), visit);
        index = 0;
    }
    if (index >= 0) visitNumberCombo_->setCurrentIndex(index);
}

// One row per delivery, stamped as it starts.
}  // namespace beam::app
