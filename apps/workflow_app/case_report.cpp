#include "treatment_internal.hpp"

namespace beam::app {

void WorkflowWindow::buildReportBody() {
    reportBody_ = new QWidget(ui_->placeholderPage);
    auto* outer = new QVBoxLayout(reportBody_);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(8);

    auto* identityGroup = new QGroupBox(QStringLiteral("Case report"), reportBody_);
    auto* identityLayout = new QVBoxLayout(identityGroup);
    identityLayout->setContentsMargins(10, 6, 10, 8);
    identityLayout->setSpacing(6);

    auto* chooser = new QHBoxLayout;
    chooser->setSpacing(8);
    auto* visitLabel = new QLabel(QStringLiteral("Visit"), identityGroup);
    visitLabel->setStyleSheet(QStringLiteral("QLabel { color: #f2f7f8; }"));
    chooser->addWidget(visitLabel);
    reportVisitCombo_ = new QComboBox(identityGroup);
    applyItemViewPalette(reportVisitCombo_);
    connect(reportVisitCombo_, &QComboBox::currentIndexChanged, this,
            [this](int) { refreshReport(); });
    chooser->addWidget(reportVisitCombo_);
    auto* operatorLabel = new QLabel(QStringLiteral("Operator ID"), identityGroup);
    operatorLabel->setStyleSheet(QStringLiteral("QLabel { color: #f2f7f8; }"));
    chooser->addWidget(operatorLabel);
    reportOperatorEdit_ = new QLineEdit(identityGroup);
    reportOperatorEdit_->setPlaceholderText(QStringLiteral("Who ran this visit"));
    // Wide enough to type an initial-and-number into, not wide enough to push the export
    reportOperatorEdit_->setMaximumWidth(160);
    styleInputField(reportOperatorEdit_);
    connect(reportOperatorEdit_, &QLineEdit::textChanged, this, [this] { refreshReport(); });
    // One operator per visit, whichever page it was typed on.
    if (operatorIdEdit_ != nullptr) {
        reportOperatorEdit_->setText(operatorIdEdit_->text());
        connect(operatorIdEdit_, &QLineEdit::textChanged, reportOperatorEdit_,
                &QLineEdit::setText);
        connect(reportOperatorEdit_, &QLineEdit::textChanged, operatorIdEdit_,
                &QLineEdit::setText);
    }
    chooser->addWidget(reportOperatorEdit_);
    chooser->addStretch(1);
    exportReportButton_ = new QPushButton(QStringLiteral("Export CSV"), identityGroup);
    exportReportButton_->setStyleSheet(
        QStringLiteral("QPushButton { padding: 7px 14px; background: #176b87; color: white; "
                       "border: 0; border-radius: 5px; font-weight: 600; } "
                       "QPushButton:disabled { background: #3b474e; color: #7e8c93; }"));
    connect(exportReportButton_, &QPushButton::clicked, this, [this] { exportCaseReport(); });
    chooser->addWidget(exportReportButton_);
    exportReportPdfButton_ = new QPushButton(QStringLiteral("Export PDF"), identityGroup);
    exportReportPdfButton_->setStyleSheet(exportReportButton_->styleSheet());
    connect(exportReportPdfButton_, &QPushButton::clicked, this,
            [this] { exportCaseReportPdf(); });
    chooser->addWidget(exportReportPdfButton_);
    saveSessionButton_ = new QPushButton(QStringLiteral("Save session..."), identityGroup);
    saveSessionButton_->setStyleSheet(exportReportButton_->styleSheet());
    saveSessionButton_->setToolTip(
        QStringLiteral("Writes this patient's record, the MRI and the registration to one file, "
                       "so the next visit can resume without repeating them."));
    connect(saveSessionButton_, &QPushButton::clicked, this, [this] { saveTreatmentSessionFile(); });
    chooser->addWidget(saveSessionButton_);
    identityLayout->addLayout(chooser);

    reportIdentityLabel_ = new QLabel(identityGroup);
    reportIdentityLabel_->setWordWrap(true);
    reportIdentityLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    reportIdentityLabel_->setStyleSheet(QStringLiteral(
        "QLabel { padding: 8px 10px; background: #eef2f4; color: #17313f; border-radius: 4px; }"));
    identityLayout->addWidget(reportIdentityLabel_);
    outer->addWidget(identityGroup);

    auto* deliveredGroup = new QGroupBox(QStringLiteral("What was delivered"), reportBody_);
    auto* deliveredLayout = new QVBoxLayout(deliveredGroup);
    deliveredLayout->setContentsMargins(10, 6, 10, 8);
    reportTable_ = new QTableWidget(0, 9, deliveredGroup);
    reportTable_->setHorizontalHeaderLabels(
        {QStringLiteral("#"), QStringLiteral("Target"), QStringLiteral("Started"),
         QStringLiteral("Ended"), QStringLiteral("Duration (s)"), QStringLiteral("Amplitude"),
         QStringLiteral("Sham"), QStringLiteral("Outcome"), QStringLiteral("Notes")});
    reportTable_->verticalHeader()->setVisible(false);
    reportTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    reportTable_->setAlternatingRowColors(true);
    reportTable_->setShowGrid(false);
    if (patientTable_ != nullptr) {
        reportTable_->setStyleSheet(patientTable_->styleSheet());
        reportTable_->horizontalHeader()->setStyleSheet(
            patientTable_->horizontalHeader()->styleSheet());
    }
    reportTable_->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    for (int column = 0; column < reportTable_->columnCount() - 1; ++column)
        reportTable_->horizontalHeader()->setSectionResizeMode(column,
                                                               QHeaderView::ResizeToContents);
    reportTable_->horizontalHeader()->setStretchLastSection(true);
    deliveredLayout->addWidget(reportTable_);
    outer->addWidget(deliveredGroup, 1);

    auto* pastGroup = new QGroupBox(QStringLiteral("Reports already produced"), reportBody_);
    auto* pastLayout = new QVBoxLayout(pastGroup);
    pastLayout->setContentsMargins(10, 6, 10, 8);
    pastLayout->setSpacing(6);
    auto* pastHint = new QLabel(
        QStringLiteral("Kept with this patient's record. Double-click to open one."), pastGroup);
    pastHint->setStyleSheet(QStringLiteral("color: #7e8c93;"));
    pastLayout->addWidget(pastHint);
    pastReportsList_ = new QListWidget(pastGroup);
    pastReportsList_->setMaximumHeight(120);
    pastReportsList_->setStyleSheet(QStringLiteral(
        "QListWidget { background: #ffffff; color: #17313f; border: 1px solid #39758e; } "
        "QListWidget::item:selected { background: #58c7e5; color: #083d4d; }"));
    // Handed to whatever the machine opens PDFs with.
    connect(pastReportsList_, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem* item) {
                if (item == nullptr) return;
                const QString path = item->data(Qt::UserRole).toString();
                if (path.isEmpty()) return;
                if (!QDesktopServices::openUrl(QUrl::fromLocalFile(path))) {
                    showMessage(QStringLiteral("%1 could not be opened.")
                                    .arg(QDir::toNativeSeparators(path)),
                                true);
                }
            });
    pastLayout->addWidget(pastReportsList_);
    outer->addWidget(pastGroup);

    reportBody_->setVisible(false);
}

// The CRF metadata for the visit on show, built by the ported setSessionCaseReportParameters.m
beam::gui::CaseReportForm WorkflowWindow::reportFormForVisit(int visit) const {
    beam::gui::SessionCrfInputs inputs;
    beam::gui::Patient open;
    if (patients_.find(currentPatientId_, &open)) inputs.participantId = open.externalId;
    inputs.visitNumber = visit;
    inputs.hydrogelSize =
        hydrogelSizeCombo_ != nullptr ? hydrogelSizeCombo_->currentText().toStdString() : "Small";
    // The visit is dated by its first delivery, not by today: a report run a week later still
    QDateTime when = QDateTime::currentDateTime();
    for (const beam::gui::TreatmentLogRow& row :
         treatmentLog_.rowsFor(currentProtocolName().toStdString(), visit)) {
        if (row.startedAt.empty()) continue;
        const QDateTime stamp =
            QDateTime::fromString(QString::fromStdString(row.startedAt), Qt::ISODateWithMs);
        if (stamp.isValid()) {
            when = stamp;
            break;
        }
    }
    inputs.year = when.date().year();
    inputs.month = when.date().month();
    inputs.day = when.date().day();
    inputs.hour = when.time().hour();
    inputs.minute = when.time().minute();
    inputs.second = when.time().second();

    beam::gui::CaseReportForm crf = beam::gui::buildSessionCaseReportForm(inputs);
    // setSessionCaseReportParameters.m hardcodes a site; this device knows the real one, and
    if (!open.site.empty()) crf.siteId = open.site;
    if (reportOperatorEdit_ != nullptr)
        crf.operatorId = reportOperatorEdit_->text().trimmed().toStdString();
    return crf;
}

void WorkflowWindow::refreshReport() {
    if (reportBody_ == nullptr || reportTable_ == nullptr) return;
    const std::string protocol = currentProtocolName().toStdString();

    // Every visit this patient has, newest first: a report is usually wanted for the one just
    std::set<int> visits;
    for (const beam::gui::TreatmentLogRow& row : treatmentLog_.rows())
        if (row.protocol == protocol) visits.insert(row.visit);
    const int wanted = reportVisitCombo_->currentData().isValid()
                           ? reportVisitCombo_->currentData().toInt()
                           : currentVisitNumber();
    {
        const QSignalBlocker blocker(reportVisitCombo_);
        reportVisitCombo_->clear();
        for (auto visit = visits.rbegin(); visit != visits.rend(); ++visit)
            reportVisitCombo_->addItem(QStringLiteral("Visit %1").arg(*visit), *visit);
        const int index = reportVisitCombo_->findData(wanted);
        if (index >= 0) reportVisitCombo_->setCurrentIndex(index);
    }
    const int visit = reportVisitCombo_->currentData().isValid()
                          ? reportVisitCombo_->currentData().toInt()
                          : 0;

    beam::gui::Patient open;
    if (!patients_.find(currentPatientId_, &open)) {
        reportIdentityLabel_->setText(
            QStringLiteral("No patient is open. Accept a case to produce a report."));
        reportTable_->setRowCount(0);
        exportReportButton_->setEnabled(false);
        exportReportPdfButton_->setEnabled(false);
        refreshPastReports();
        return;
    }

    const std::vector<beam::gui::TreatmentLogRow> rows = treatmentLog_.rowsFor(protocol, visit);
    int delivered = 0;
    int shams = 0;
    int stopped = 0;
    double seconds = 0.0;
    for (const beam::gui::TreatmentLogRow& row : rows) {
        if (!beam::gui::TreatmentLog::delivered(row)) continue;
        ++delivered;
        if (row.sham) ++shams;
        if (beam::gui::TreatmentLog::stopped(row)) ++stopped;
        seconds += row.endTimeSeconds - row.startTimeSeconds;
    }

    const beam::gui::CaseReportForm crf = reportFormForVisit(visit);
    const QString born =
        open.dateOfBirth.empty() ? QString::fromUtf8("\xe2\x80\x94")
                                 : QString::fromStdString(open.dateOfBirth);
    reportIdentityLabel_->setText(
        QStringLiteral("<b>%1</b> &nbsp; Participant %2 &nbsp; Site %3 &nbsp; born %4"
                       "<br>Visit %5 of \"%6\" &nbsp; %7-%8-%9 &nbsp; hydrogel %10"
                       "<br>%11 sonication(s) delivered, %12 sham, %13 stopped early, "
                       "%14 s total output"
                       "<br>Export filename: <tt>%15.csv</tt>")
            .arg(QString::fromStdString(beam::gui::patientDisplayName(open)),
                 QString::fromStdString(open.externalId), QString::fromStdString(crf.siteId),
                 born)
            .arg(visit)
            .arg(currentProtocolName())
            .arg(crf.year, 4, 10, QLatin1Char('0'))
            .arg(crf.month, 2, 10, QLatin1Char('0'))
            .arg(crf.day, 2, 10, QLatin1Char('0'))
            .arg(QString::fromStdString(crf.hydrogelSize))
            .arg(delivered)
            .arg(shams)
            .arg(stopped)
            .arg(seconds, 0, 'f', 1)
            .arg(caseReportFileStem(open, crf, visit)));

    reportTable_->setRowCount(static_cast<int>(rows.size()));
    for (std::size_t index = 0; index < rows.size(); ++index) {
        const beam::gui::TreatmentLogRow& row = rows[index];
        const auto cell = [](const QString& text) { return new QTableWidgetItem(text); };
        const auto timeOfDay = [](const std::string& stamp) {
            const QDateTime when =
                QDateTime::fromString(QString::fromStdString(stamp), Qt::ISODateWithMs);
            return when.isValid() ? when.toString(QStringLiteral("HH:mm:ss"))
                                  : QString::fromUtf8("\xe2\x80\x94");
        };
        const int at = static_cast<int>(index);
        reportTable_->setItem(at, 0, cell(QString::number(row.sequence)));
        reportTable_->setItem(at, 1, cell(QString::fromStdString(row.target)));
        reportTable_->setItem(at, 2, cell(timeOfDay(row.startedAt)));
        reportTable_->setItem(at, 3, cell(timeOfDay(row.endedAt)));
        reportTable_->setItem(
            at, 4, cell(QString::number(row.endTimeSeconds - row.startTimeSeconds, 'f', 1)));
        reportTable_->setItem(at, 5, cell(QString::number(row.amplitude)));
        reportTable_->setItem(at, 6, cell(row.sham ? QStringLiteral("yes") : QString()));
        reportTable_->setItem(
            at, 7,
            cell(!beam::gui::TreatmentLog::delivered(row) ? QStringLiteral("not delivered")
                 : beam::gui::TreatmentLog::stopped(row)  ? QStringLiteral("stopped early")
                                                          : QStringLiteral("completed")));
        QString notes = QString::fromStdString(row.notes);
        notes.replace(QLatin1Char('\n'), QString::fromUtf8(" \xc2\xb7 "));
        reportTable_->setItem(at, 8, cell(notes));
    }
    // A visit that delivered nothing has no case to report.
    exportReportButton_->setEnabled(delivered > 0);
    exportReportPdfButton_->setEnabled(delivered > 0);
    refreshPastReports();
}

void WorkflowWindow::exportCaseReport() {
    const int visit = reportVisitCombo_ != nullptr && reportVisitCombo_->currentData().isValid()
                          ? reportVisitCombo_->currentData().toInt()
                          : 0;
    beam::gui::Patient open;
    if (visit <= 0 || !patients_.find(currentPatientId_, &open)) return;

    const beam::gui::CaseReportForm crf = reportFormForVisit(visit);
    const QString reports = patientReportsDirectory();
    const QString suggested =
        QDir(reports.isEmpty() ? patientDataDirectory() : reports)
            .filePath(caseReportFileStem(open, crf, visit) + QStringLiteral(".csv"));
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Export case report"),
                                                      suggested, QStringLiteral("CSV (*.csv)"));
    if (path.isEmpty()) return;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        showMessage(QStringLiteral("%1 could not be written.").arg(QDir::toNativeSeparators(path)),
                    true);
        return;
    }
    QTextStream out(&file);
    const auto quoted = [](const QString& text) {
        QString escaped = text;
        escaped.replace(QLatin1Char('"'), QStringLiteral("\"\""));
        escaped.replace(QLatin1Char('\n'), QString::fromUtf8(" \xc2\xb7 "));
        return QStringLiteral("\"%1\"").arg(escaped);
    };
    // The metadata first as name/value pairs, then the deliveries.
    out << "Field,Value\n";
    const auto pair = [&](const char* name, const QString& value) {
        out << name << ',' << quoted(value) << '\n';
    };
    pair("SubjectID", QString::fromStdString(crf.subjectId));
    pair("SubjectName", QString::fromStdString(beam::gui::patientDisplayName(open)));
    pair("DateOfBirth", QString::fromStdString(open.dateOfBirth));
    pair("SiteID", QString::fromStdString(crf.siteId));
    pair("OperatorID", QString::fromStdString(crf.operatorId));
    pair("VisitNumber", QString::number(crf.visitNumber));
    pair("Protocol", currentProtocolName());
    pair("HydrogelSize", QString::fromStdString(crf.hydrogelSize));
    pair("SessionDate", QStringLiteral("%1-%2-%3")
                            .arg(crf.year, 4, 10, QLatin1Char('0'))
                            .arg(crf.month, 2, 10, QLatin1Char('0'))
                            .arg(crf.day, 2, 10, QLatin1Char('0')));
    pair("BEAMSerialNumber", QString::fromStdString(crf.snBeam));
    pair("Transducer1SerialNumber", QString::fromStdString(crf.snTransducer1));
    pair("Transducer2SerialNumber", QString::fromStdString(crf.snTransducer2));
    pair("SoftwareVersion", QString::fromStdString(crf.softwareVersion));
    // From the visit's own rows, not from what is plugged in now: an export of an old visit has
    const auto recorded = [&](std::string beam::gui::TreatmentLogRow::*field,
                              const QString& fallback) {
        for (const beam::gui::TreatmentLogRow& row :
             treatmentLog_.rowsFor(currentProtocolName().toStdString(), visit))
            if (!(row.*field).empty()) return QString::fromStdString(row.*field);
        return fallback;
    };
    pair("MCUModel", recorded(&beam::gui::TreatmentLogRow::deviceModel,
                              deviceLink_ != nullptr ? deviceLink_->board() : QString()));
    pair("MCUSerialNumber", recorded(&beam::gui::TreatmentLogRow::deviceSerial,
                                     deviceLink_ != nullptr ? deviceLink_->serial() : QString()));
    pair("CalDuty", recorded(&beam::gui::TreatmentLogRow::calDuty,
                             QString::fromStdString(calibrationDutyText())));
    pair("CalPressure", recorded(&beam::gui::TreatmentLogRow::calPressure,
                                 QString::fromStdString(calibrationPressureText())));
    pair("MRIFile", recorded(&beam::gui::TreatmentLogRow::mriFile, mriPath_));
    pair("MRIFingerprint",
         recorded(&beam::gui::TreatmentLogRow::mriFingerprint, mriFingerprint_));
    pair("Fiducials", recorded(&beam::gui::TreatmentLogRow::fiducialsMm, fiducialsAsText()));
    {
        const auto rows = treatmentLog_.rowsFor(currentProtocolName().toStdString(), visit);
        const QString lock =
            rows.empty() ? QString()
                         : QStringLiteral("H%1V%2")
                               .arg(rows.front().lockHorizontal)
                               .arg(rows.front().lockVertical);
        pair("LockPosition", lock);
    }
    pair("ExportedAt", QDateTime::currentDateTime().toString(Qt::ISODate));
    out << '\n';
    out << "Number,Target,Started,Ended,DurationSeconds,Amplitude,BD,BI,PD,PI,Sham,Outcome,"
           "ResponsePain,ResponseMood,Notes\n";
    for (const beam::gui::TreatmentLogRow& row :
         treatmentLog_.rowsFor(currentProtocolName().toStdString(), visit)) {
        out << row.sequence << ',' << quoted(QString::fromStdString(row.target)) << ','
            << quoted(QString::fromStdString(row.startedAt)) << ','
            << quoted(QString::fromStdString(row.endedAt)) << ','
            << (row.endTimeSeconds - row.startTimeSeconds) << ',' << row.amplitude << ','
            << row.burstDurationSeconds << ',' << row.burstIntervalSeconds << ','
            << row.pulseDurationSeconds << ',' << row.pulseIntervalSeconds << ','
            << (row.sham ? 1 : 0) << ','
            << quoted(!beam::gui::TreatmentLog::delivered(row) ? QStringLiteral("not delivered")
                      : beam::gui::TreatmentLog::stopped(row)  ? QStringLiteral("stopped early")
                                                               : QStringLiteral("completed"))
            << ',';
        if (row.responsePain) out << *row.responsePain;
        out << ',';
        if (row.responseMood) out << *row.responseMood;
        out << ',' << quoted(QString::fromStdString(row.notes)) << '\n';
    }
    file.close();
    refreshPastReports();
    showMessage(QStringLiteral("Case report written to %1").arg(QDir::toNativeSeparators(path)),
                false);
}

// BeamV0's own computeCrfAutoSaveFilename, sanitised for the filesystem.
QString WorkflowWindow::caseReportFileStem(const beam::gui::Patient& patient,
                                           const beam::gui::CaseReportForm& crf,
                                           int visit) const {
    Q_UNUSED(patient);
    Q_UNUSED(visit);
    QString stem = QString::fromStdString(beam::gui::computeCrfAutoSaveFilename(crf));
    stem.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]+")), QStringLiteral("_"));
    return stem.isEmpty() ? QStringLiteral("case-report") : stem;
}

// Where a patient's case reports are kept.
QString WorkflowWindow::patientReportsDirectory() const {
    const QString patientDirectory = patientDataDirectory();
    if (patientDirectory.isEmpty()) return QString();
    const QString reports = QDir(patientDirectory).filePath(QStringLiteral("reports"));
    QDir().mkpath(reports);
    return reports;
}

// A CSV is what a monitor loads; this is what gets printed and filed.
QString WorkflowWindow::caseReportHtml(int visit) const {
    beam::gui::Patient open;
    if (!patients_.find(currentPatientId_, &open)) return QString();
    const beam::gui::CaseReportForm crf = reportFormForVisit(visit);
    const std::vector<beam::gui::TreatmentLogRow> rows =
        treatmentLog_.rowsFor(currentProtocolName().toStdString(), visit);

    int delivered = 0;
    int shams = 0;
    int stopped = 0;
    double seconds = 0.0;
    for (const beam::gui::TreatmentLogRow& row : rows) {
        if (!beam::gui::TreatmentLog::delivered(row)) continue;
        ++delivered;
        if (row.sham) ++shams;
        if (beam::gui::TreatmentLog::stopped(row)) ++stopped;
        seconds += row.endTimeSeconds - row.startTimeSeconds;
    }

    // One ink, one accent, two greys.
    const QString kInk = QStringLiteral("#1b2a33");
    const QString kMuted = QStringLiteral("#62757e");
    const QString kRule = QStringLiteral("#ccd8dd");
    const QString kAccent = QStringLiteral("#176b87");
    const QString kBand = QStringLiteral("#eef3f5");
    const QString kZebra = QStringLiteral("#f6f9fa");
    const QString kDash = QString::fromUtf8("\xe2\x80\x94");

    const auto esc = [](const QString& text) { return text.toHtmlEscaped(); };
    const auto orDash = [&](const QString& text) { return text.isEmpty() ? kDash : esc(text); };
    const auto str = [](const std::string& text) { return QString::fromStdString(text); };
    const auto timeOfDay = [&](const std::string& stamp) {
        const QDateTime when = QDateTime::fromString(str(stamp), Qt::ISODateWithMs);
        return when.isValid() ? when.toString(QStringLiteral("HH:mm:ss")) : kDash;
    };
    const QString sessionDate = QStringLiteral("%1-%2-%3")
                                    .arg(crf.year, 4, 10, QLatin1Char('0'))
                                    .arg(crf.month, 2, 10, QLatin1Char('0'))
                                    .arg(crf.day, 2, 10, QLatin1Char('0'));

    // A full-width band of solid colour, which is how a rule is drawn here: border on an empty
    const auto rule = [&](const QString& colour, int points) {
        return QStringLiteral("<table width=\"100%\" cellspacing=\"0\" cellpadding=\"0\">"
                              "<tr><td style=\"background-color:%1; font-size:%2pt\">&nbsp;</td>"
                              "</tr></table>")
            .arg(colour)
            .arg(points);
    };
    const auto sectionTitle = [&](const QString& text) {
        return QStringLiteral("<p style=\"margin-top:16pt; margin-bottom:3pt; color:%1; "
                              "font-size:8pt; font-weight:bold\">%2</p>")
                   .arg(kAccent, esc(text)) +
               rule(kRule, 1);
    };
    // Key on the left in grey, value on the right in ink.
    const auto field = [&](const QString& name, const QString& value) {
        return QStringLiteral("<tr>"
                              "<td width=\"42%\" style=\"padding:3pt 6pt 3pt 0; color:%1\">%2</td>"
                              "<td style=\"padding:3pt 0; color:%3\">%4</td>"
                              "</tr>")
            .arg(kMuted, esc(name), kInk, orDash(value));
    };

    QString html = QStringLiteral(
                       "<html><body style=\"font-family:'Helvetica','Arial',sans-serif; "
                       "font-size:9pt; color:%1\">")
                       .arg(kInk);

    // Masthead: what this is on the left, who it is about on the right.
    html += QStringLiteral(
                "<table width=\"100%\" cellspacing=\"0\" cellpadding=\"0\"><tr>"
                "<td valign=\"bottom\" style=\"color:%1; font-size:17pt; font-weight:bold\">"
                "CASE REPORT</td>"
                "<td valign=\"bottom\" align=\"right\" style=\"font-size:9pt; color:%2\">"
                "<b style=\"color:%3; font-size:11pt\">%4</b><br>%5 &nbsp;&middot;&nbsp; %6"
                "</td></tr></table>")
                .arg(kAccent, kMuted, kInk,
                     esc(str(beam::gui::patientDisplayName(open))),
                     esc(str(crf.subjectId)), esc(str(crf.siteId)));
    html += rule(kAccent, 3);

    // The two things a reader checks first, side by side.
    html += QStringLiteral("<table width=\"100%\" cellspacing=\"0\" cellpadding=\"0\"><tr>"
                           "<td width=\"48%\" valign=\"top\">");
    html += sectionTitle(QStringLiteral("PARTICIPANT"));
    html += QStringLiteral("<table width=\"100%\" cellspacing=\"0\" cellpadding=\"0\">");
    html += field(QStringLiteral("Participant ID"), str(crf.subjectId));
    html += field(QStringLiteral("Name"), str(beam::gui::patientDisplayName(open)));
    html += field(QStringLiteral("Date of birth"), str(open.dateOfBirth));
    html += field(QStringLiteral("Site ID"), str(crf.siteId));
    html += QStringLiteral("</table></td><td width=\"4%\"></td>"
                           "<td width=\"48%\" valign=\"top\">");
    html += sectionTitle(QStringLiteral("SESSION"));
    html += QStringLiteral("<table width=\"100%\" cellspacing=\"0\" cellpadding=\"0\">");
    html += field(QStringLiteral("Visit number"), QString::number(crf.visitNumber));
    html += field(QStringLiteral("Date"), sessionDate);
    html += field(QStringLiteral("Protocol"), currentProtocolName());
    html += field(QStringLiteral("Operator"), str(crf.operatorId));
    html += field(QStringLiteral("Hydrogel size"), str(crf.hydrogelSize));
    html += QStringLiteral("</table></td></tr></table>");

    // The four numbers somebody skims for, given room to be skimmed.
    const auto figure = [&](const QString& value, const QString& label, bool last) {
        return QStringLiteral("<td width=\"25%\" align=\"center\" "
                              "style=\"background-color:%1; padding:7pt 4pt; %2\">"
                              "<span style=\"font-size:15pt; font-weight:bold; color:%3\">%4"
                              "</span><br>"
                              "<span style=\"font-size:7.5pt; color:%5\">%6</span></td>")
            .arg(kBand,
                 last ? QString()
                      : QStringLiteral("border-right:2px solid #ffffff"),
                 kAccent, esc(value), kMuted, esc(label));
    };
    html += QStringLiteral("<p style=\"margin-top:14pt; margin-bottom:0\"></p>");
    html += QStringLiteral("<table width=\"100%\" cellspacing=\"0\" cellpadding=\"0\"><tr>");
    html += figure(QString::number(delivered), QStringLiteral("sonications delivered"), false);
    html += figure(QString::number(shams), QStringLiteral("of which sham"), false);
    html += figure(QString::number(stopped), QStringLiteral("stopped early"), false);
    html += figure(QStringLiteral("%1 s").arg(seconds, 0, 'f', 1),
                   QStringLiteral("total output"), true);
    html += QStringLiteral("</tr></table>");

    html += sectionTitle(QStringLiteral("SONICATIONS"));
    // Qt honours neither per-cell padding nor cellpadding here, so the gap between a
    const auto headCell = [&](const QString& text, const QString& align,
                              const QString& width = QString()) {
        return QStringLiteral("<td align=\"%1\"%2 style=\"background-color:%3; color:%4; "
                              "font-size:7.5pt; font-weight:bold; "
                              "border-bottom:1px solid %5\">%6</td>")
            .arg(align,
                 width.isEmpty() ? QString() : QStringLiteral(" width=\"%1\"").arg(width),
                 kBand, kMuted, kRule, esc(text));
    };
    // cellpadding, not per-cell padding: Qt's rich text honours the table attribute and ignores
    html += QStringLiteral("<table width=\"100%\" cellspacing=\"0\" cellpadding=\"4\" "
                           "style=\"font-size:8pt\"><tr>");
    // The sequence number needs room on its right or it runs into the target name: one is
    html += headCell(QStringLiteral("#"), QStringLiteral("left"), QStringLiteral("5%"));
    html += headCell(QStringLiteral("Target"), QStringLiteral("left"));
    html += headCell(QStringLiteral("Started"), QStringLiteral("left"));
    html += headCell(QStringLiteral("Ended"), QStringLiteral("left"));
    html += headCell(QStringLiteral("Dur s"), QStringLiteral("right"));
    html += headCell(QStringLiteral("Amp"), QStringLiteral("right"));
    html += headCell(QStringLiteral("BD"), QStringLiteral("right"));
    html += headCell(QStringLiteral("BI"), QStringLiteral("right"));
    html += headCell(QStringLiteral("PD"), QStringLiteral("right"));
    html += headCell(QStringLiteral("PI"), QStringLiteral("right"));
    html += headCell(QStringLiteral("Sham"), QStringLiteral("center"));
    html += headCell(QStringLiteral("Outcome"), QStringLiteral("left"));
    html += headCell(QStringLiteral("Pain"), QStringLiteral("right"));
    html += headCell(QStringLiteral("Mood"), QStringLiteral("right"));
    html += QStringLiteral("</tr>");

    int index = 0;
    for (const beam::gui::TreatmentLogRow& row : rows) {
        // Striped in the generated markup: Qt's CSS has no nth-child.
        const QString background = (index++ % 2) == 1 ? kZebra : QStringLiteral("#ffffff");
        const bool ran = beam::gui::TreatmentLog::delivered(row);
        // A row that never fired is greyed, so the eye separates what was given from what was
        const QString ink = ran ? kInk : kMuted;
        const auto cell = [&](const QString& text, const QString& align,
                             const QString& width = QString()) {
            return QStringLiteral("<td align=\"%1\"%2 style=\"background-color:%3; color:%4; "
                                  "border-bottom:1px solid %5\">%6</td>")
                .arg(align,
                     width.isEmpty() ? QString() : QStringLiteral(" width=\"%1\"").arg(width),
                     background, ink, kRule,
                     text.isEmpty() ? QString() : esc(text));
        };
        const QString outcome = !ran ? QStringLiteral("not delivered")
                                : beam::gui::TreatmentLog::stopped(row)
                                    ? QStringLiteral("stopped early")
                                    : QStringLiteral("completed");
        html += QStringLiteral("<tr>");
        html += cell(QString::number(row.sequence), QStringLiteral("left"), QStringLiteral("5%"));
        html += cell(str(row.target), QStringLiteral("left"));
        html += cell(timeOfDay(row.startedAt), QStringLiteral("left"));
        html += cell(timeOfDay(row.endedAt), QStringLiteral("left"));
        html += cell(QString::number(row.endTimeSeconds - row.startTimeSeconds, 'f', 1),
                     QStringLiteral("right"));
        html += cell(QString::number(row.amplitude), QStringLiteral("right"));
        html += cell(QString::number(row.burstDurationSeconds), QStringLiteral("right"));
        html += cell(QString::number(row.burstIntervalSeconds), QStringLiteral("right"));
        html += cell(QString::number(row.pulseDurationSeconds), QStringLiteral("right"));
        html += cell(QString::number(row.pulseIntervalSeconds), QStringLiteral("right"));
        html += cell(row.sham ? QStringLiteral("yes") : QString(), QStringLiteral("center"));
        html += cell(outcome, QStringLiteral("left"));
        html += cell(row.responsePain ? QString::number(*row.responsePain) : QString(),
                     QStringLiteral("right"));
        html += cell(row.responseMood ? QString::number(*row.responseMood) : QString(),
                     QStringLiteral("right"));
        html += QStringLiteral("</tr>");
    }
    html += QStringLiteral("</table>");
    if (rows.empty()) {
        html += QStringLiteral("<p style=\"color:%1; font-size:8.5pt\">Nothing was scheduled "
                               "for this visit.</p>")
                    .arg(kMuted);
    }

    // Notes get their own section: they run to several lines and would make the table above
    QString notes;
    for (const beam::gui::TreatmentLogRow& row : rows) {
        if (row.notes.empty()) continue;
        notes += QStringLiteral("<tr>"
                                "<td width=\"14%\" valign=\"top\" style=\"padding:3pt 6pt 3pt 0; "
                                "color:%1\">Sonication %2</td>"
                                "<td style=\"padding:3pt 0\">%3</td></tr>")
                     .arg(kMuted)
                     .arg(row.sequence)
                     .arg(esc(str(row.notes)).replace(QLatin1Char('\n'), QStringLiteral("<br>")));
    }
    if (!notes.isEmpty()) {
        html += sectionTitle(QStringLiteral("NOTES"));
        html += QStringLiteral("<table width=\"100%\" cellspacing=\"0\" cellpadding=\"0\" "
                               "style=\"font-size:8.5pt\">") +
                notes + QStringLiteral("</table>");
    }

    html += sectionTitle(QStringLiteral("DEVICE"));
    html += QStringLiteral("<table width=\"100%\" cellspacing=\"0\" cellpadding=\"0\"><tr>"
                           "<td width=\"48%\" valign=\"top\">"
                           "<table width=\"100%\" cellspacing=\"0\" cellpadding=\"0\">");
    html += field(QStringLiteral("BEAM serial number"), str(crf.snBeam));
    html += field(QStringLiteral("Software version"), str(crf.softwareVersion));
    html += QStringLiteral("</table></td><td width=\"4%\"></td>"
                           "<td width=\"48%\" valign=\"top\">"
                           "<table width=\"100%\" cellspacing=\"0\" cellpadding=\"0\">");
    html += field(QStringLiteral("Transducer 1"), str(crf.snTransducer1));
    html += field(QStringLiteral("Transducer 2"), str(crf.snTransducer2));
    html += QStringLiteral("</table></td></tr></table>");

    html += QStringLiteral("<p style=\"margin-top:18pt; margin-bottom:0\"></p>");
    html += rule(kRule, 1);
    html += QStringLiteral("<p style=\"margin-top:4pt; font-size:7.5pt; color:%1\">"
                           "Produced by Beam on %2 from this patient's treatment log. "
                           "A blank field is one Beam does not collect yet.</p>")
                .arg(kMuted, esc(QDateTime::currentDateTime().toString(Qt::ISODate)));
    html += QStringLiteral("</body></html>");
    return html;
}

void WorkflowWindow::exportCaseReportPdf() {
    const int visit = reportVisitCombo_ != nullptr && reportVisitCombo_->currentData().isValid()
                          ? reportVisitCombo_->currentData().toInt()
                          : 0;
    beam::gui::Patient open;
    if (visit <= 0 || !patients_.find(currentPatientId_, &open)) return;

    const beam::gui::CaseReportForm crf = reportFormForVisit(visit);
    const QString reports = patientReportsDirectory();
    const QString suggested =
        QDir(reports.isEmpty() ? patientDataDirectory() : reports)
            .filePath(caseReportFileStem(open, crf, visit) + QStringLiteral(".pdf"));
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Export case report"),
                                                      suggested, QStringLiteral("PDF (*.pdf)"));
    if (path.isEmpty()) return;

    QPdfWriter writer(path);
    writer.setPageSize(QPageSize(QPageSize::A4));
    writer.setPageMargins(QMarginsF(14, 14, 14, 14), QPageLayout::Millimeter);
    writer.setTitle(QStringLiteral("Case report — %1 visit %2")
                        .arg(QString::fromStdString(open.externalId))
                        .arg(visit));
    QTextDocument document;
    // No setPageSize: those would be device units, and a document given a page size is treated
    document.setDefaultFont(QFont(QStringLiteral("Helvetica"), 10));
    document.setHtml(caseReportHtml(visit));
    document.print(&writer);

    if (!QFileInfo::exists(path)) {
        showMessage(QStringLiteral("%1 could not be written.").arg(QDir::toNativeSeparators(path)),
                    true);
        return;
    }
    refreshPastReports();
    showMessage(QStringLiteral("Case report written to %1").arg(QDir::toNativeSeparators(path)),
                false);
}

// What has already been produced for this patient.
void WorkflowWindow::refreshPastReports() {
    if (pastReportsList_ == nullptr) return;
    pastReportsList_->clear();
    const QString reports = patientReportsDirectory();
    if (reports.isEmpty()) {
        pastReportsList_->addItem(QStringLiteral("No patient is open."));
        pastReportsList_->setEnabled(false);
        return;
    }
    pastReportsList_->setEnabled(true);
    QDir directory(reports);
    directory.setNameFilters({QStringLiteral("*.pdf"), QStringLiteral("*.csv")});
    // Newest first: the one just made is the one being looked for.
    const QFileInfoList files = directory.entryInfoList(QDir::Files, QDir::Time);
    if (files.isEmpty()) {
        pastReportsList_->addItem(
            QStringLiteral("No reports yet for this patient. Exporting one puts it here."));
        return;
    }
    for (const QFileInfo& file : files) {
        auto* item = new QListWidgetItem(
            QStringLiteral("%1    %2")
                .arg(file.fileName(),
                     file.lastModified().toString(QStringLiteral("yyyy-MM-dd HH:mm"))));
        item->setData(Qt::UserRole, file.absoluteFilePath());
        item->setToolTip(QDir::toNativeSeparators(file.absoluteFilePath()));
        pastReportsList_->addItem(item);
    }
}

// Reads a file the application itself wrote, so the session carries exactly what is on disk
QString WorkflowWindow::patientFileText(const QString& name) const {
    const QString directory = patientDataDirectory();
    if (directory.isEmpty()) return QString();
    QFile file(QDir(directory).filePath(name));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return QString();
    return QString::fromUtf8(file.readAll());
}

}  // namespace beam::app
