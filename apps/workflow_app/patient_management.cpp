#include "treatment_internal.hpp"

#include "gui/treatment_log_db.hpp"

namespace beam::app {

QString WorkflowWindow::currentProtocolName() const {
    return treatmentProtocolCombo_ ? treatmentProtocolCombo_->currentText() : QString();
}

// data/runtime/ inside the repository: edits sit with the project rather than in a per-user
QString WorkflowWindow::treatmentDataDirectory() const {
    const QString executableDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QDir(executableDir).filePath(QStringLiteral("../../../data")),
        QDir(executableDir).filePath(QStringLiteral("../../data")),
        QDir(executableDir).filePath(QStringLiteral("data")),
        QDir::current().filePath(QStringLiteral("data"))};
    for (const QString& candidate : candidates) {
        const QDir directory(candidate);
        if (!directory.exists()) continue;
        const QString runtime = directory.absoluteFilePath(QStringLiteral("runtime"));
        if (QDir().mkpath(runtime)) return runtime;
    }
    const QString home =
        QDir(QDir::homePath()).filePath(QStringLiteral("Documents/BeamAI/treatment"));
    QDir().mkpath(home);
    return home;
}


// Where one person's record lives: their log, and where each of their visits was left.
QString WorkflowWindow::patientDataDirectory() const {
    if (currentPatientId_ <= 0)
        return patients_.empty() ? treatmentDataDirectory() : QString();
    const QString directory = QDir(treatmentDataDirectory())
                                  .filePath(QStringLiteral("patients/%1").arg(currentPatientId_));
    QDir().mkpath(directory);
    return directory;
}

// Who the patient is, as against which participant number they carry.
// One past the highest Participant ID already in use, counting only the ones
// that are plain numbers, so a site using its own codes is left alone.
QString WorkflowWindow::suggestedParticipantId() const {
    int highest = 0;
    for (const beam::gui::Patient& patient : patients_.patients()) {
        bool numeric = false;
        const int value = QString::fromStdString(patient.externalId).toInt(&numeric);
        if (numeric && value > highest) highest = value;
    }
    return QString::number(highest + 1);
}

void WorkflowWindow::buildCaseDemographics() {
    // Who is running the case, not who is being treated: above the patient's own fields, and
    // it survives a change of patient.
    operatorIdEdit_ = new QLineEdit(ui_->caseGroup);
    operatorIdEdit_->setPlaceholderText(QStringLiteral("Who is running this visit"));
    styleInputField(operatorIdEdit_);
    ui_->caseForm->insertRow(0, QStringLiteral("Operator ID"), operatorIdEdit_);
    connect(operatorIdEdit_, &QLineEdit::textChanged, this, [this](const QString& text) {
        const QString shown = text.trimmed();
        ui_->operatorHeader->setText(shown.isEmpty()
                                         ? QStringLiteral("Operator: —")
                                         : QStringLiteral("Operator: %1").arg(shown));
    });

    // The patient's own fields are entered in a dialog, so the page is the list of patients
    // and the one line saying which of them is open.
    createPatientDialog_ = new QDialog(this);
    createPatientDialog_->setWindowTitle(QStringLiteral("Create patient"));
    createPatientDialog_->setMinimumWidth(420);
    auto* dialogLayout = new QVBoxLayout(createPatientDialog_);
    auto* dialogForm = new QFormLayout;
    dialogLayout->addLayout(dialogForm);

    // Moved rather than rebuilt, so everything that already reads these two keeps working.
    const auto moveRow = [this, dialogForm](QLineEdit* edit) {
        QFormLayout::TakeRowResult taken = ui_->caseForm->takeRow(edit);
        QWidget* label = taken.labelItem == nullptr ? nullptr : taken.labelItem->widget();
        delete taken.labelItem;
        delete taken.fieldItem;
        edit->setParent(createPatientDialog_);
        if (label == nullptr) {
            dialogForm->addRow(edit);
        } else {
            label->setParent(createPatientDialog_);
            dialogForm->addRow(label, edit);
        }
    };
    moveRow(ui_->participantEdit);
    moveRow(ui_->siteEdit);

    const auto add = [this, dialogForm](const QString& label, const QString& placeholder) {
        auto* edit = new QLineEdit(createPatientDialog_);
        edit->setPlaceholderText(placeholder);
        styleInputField(edit);
        dialogForm->addRow(label, edit);
        return edit;
    };
    lastNameEdit_ = add(QStringLiteral("Last name"), QStringLiteral("Optional"));
    firstNameEdit_ = add(QStringLiteral("First name"), QStringLiteral("Optional"));
    // The format in the label, not only the placeholder: a placeholder is gone the moment
    dateOfBirthEdit_ =
        add(QStringLiteral("Date of birth (YYYY-MM-DD)"), QStringLiteral("Optional"));
    // Typed plainly. An input mask returned "--" for an empty field, which the validation

    auto* dialogButtons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                               createPatientDialog_);
    dialogButtons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Create"));
    dialogLayout->addWidget(dialogButtons);
    connect(dialogButtons, &QDialogButtonBox::accepted, createPatientDialog_, &QDialog::accept);
    connect(dialogButtons, &QDialogButtonBox::rejected, createPatientDialog_, &QDialog::reject);

    casePatientLabel_ = new QLabel(ui_->caseGroup);
    casePatientLabel_->setStyleSheet(QStringLiteral("QLabel { color: #dcebef; }"));
    ui_->caseForm->insertRow(1, QStringLiteral("Patient"), casePatientLabel_);

    // The header and the page line both name whoever the fields hold.
    const auto follow = [this] {
        const beam::gui::Patient typed = caseDemographics();
        const QString name = QString::fromStdString(beam::gui::patientDisplayName(typed));
        ui_->participantHeader->setText(
            name.isEmpty() ? QStringLiteral("Participant: —")
                           : QStringLiteral("Participant: %1").arg(name));
        QStringList parts;
        if (!typed.externalId.empty()) parts << QString::fromStdString(typed.externalId);
        if (!name.isEmpty()) parts << name;
        if (!typed.site.empty()) parts << QString::fromStdString(typed.site);
        casePatientLabel_->setText(
            parts.isEmpty()
                ? QStringLiteral("No patient open. Create one, or pick a row below.")
                : parts.join(QStringLiteral("   ·   ")));
    };
    for (QLineEdit* edit : {ui_->participantEdit, ui_->siteEdit, lastNameEdit_, firstNameEdit_,
                            dateOfBirthEdit_})
        connect(edit, &QLineEdit::textChanged, this, follow);
    follow();

    // Two steps, because they are two decisions.
    createPatientButton_ = new QPushButton(QStringLiteral("Create patient…"), ui_->caseGroup);
    createPatientButton_->setStyleSheet(
        QStringLiteral("QPushButton { padding: 9px 16px; background: #2b4a58; color: #dcebef; "
                       "border: 1px solid #39758e; border-radius: 5px; font-weight: 600; } "
                       "QPushButton:hover { background: #35596a; }"));
    createPatientButton_->setToolTip(
        QStringLiteral("Opens this patient's record and adds them to the list below, without "
                       "leaving this page."));
    connect(createPatientButton_, &QPushButton::clicked, this, [this] {
        // A new record starts blank rather than from whoever is open, but with
        // the next free Participant ID already in it.
        beam::gui::Patient fresh;
        fresh.externalId = suggestedParticipantId().toStdString();
        showCaseDemographics(fresh);
        ui_->participantEdit->setFocus(Qt::OtherFocusReason);
        ui_->participantEdit->selectAll();
        while (createPatientDialog_->exec() == QDialog::Accepted) {
            if (!caseFieldsUsable(createPatientDialog_)) continue;
            selectPatient(caseDemographics(), /*advancing=*/false);
            return;
        }
        // Cancelled, so the fields go back to naming whoever is still open.
        beam::gui::Patient open;
        showCaseDemographics(currentPatientId_ != 0 && patients_.find(currentPatientId_, &open)
                                 ? open
                                 : beam::gui::Patient{});
    });

    // The Accept button moves out of the form's own column and alongside it.
    const int acceptRow = ui_->caseForm->rowCount() - 1;
    ui_->caseForm->takeRow(acceptRow);
    auto* buttons = new QHBoxLayout;
    buttons->setSpacing(8);
    buttons->addWidget(createPatientButton_, 1);
    buttons->addWidget(ui_->acceptCaseButton, 2);
    ui_->caseForm->addRow(buttons);

    // A session names its patient, so it opens a record like the two above.
    auto* loadSession = new QPushButton(QStringLiteral("Load session file…"), ui_->caseGroup);
    // The Accept button's blue: both open a record and leave this page.
    loadSession->setStyleSheet(
        QStringLiteral("QPushButton { padding: 11px 18px; background: #176b87; color: white; "
                       "border: 0; border-radius: 5px; font-weight: 600; outline: none; } "
                       "QPushButton:hover { background: #2083a3; } "
                       "QPushButton:focus { outline: none; border: 0; }"));
    loadSession->setMinimumHeight(38);
    loadSession->setToolTip(
        QStringLiteral("Opens a saved visit: its patient, their history, the MRI, the fiducials "
                       "and the registration. The fields above are not used."));
    connect(loadSession, &QPushButton::clicked, this, [this] { chooseTreatmentSession(); });
    ui_->caseForm->addRow(loadSession);
}

// The two required fields, and a date of birth that is blank or a date.
bool WorkflowWindow::caseFieldsUsable(QWidget* messageParent) {
    const auto complain = [this, messageParent](const QString& text) {
        if (messageParent == nullptr)
            showMessage(text, true);
        else
            QMessageBox::warning(messageParent, QStringLiteral("Patient details"), text);
    };
    if (ui_->participantEdit->text().trimmed().isEmpty() ||
        ui_->siteEdit->text().trimmed().isEmpty()) {
        complain(QStringLiteral("Participant ID and Site ID are required."));
        return false;
    }
    std::string born;
    if (dateOfBirthEdit_ != nullptr && !readDateOfBirth(dateOfBirthEdit_->text(), &born)) {
        complain(QStringLiteral("\"%1\" is not a date. Use YYYY-MM-DD, or leave the date of "
                                "birth blank.")
                     .arg(dateOfBirthEdit_->text().trimmed()));
        dateOfBirthEdit_->setFocus(Qt::OtherFocusReason);
        dateOfBirthEdit_->selectAll();
        return false;
    }
    // Show what was stored rather than what was typed: "1999-1-1" becomes "1999-01-01", so the
    if (dateOfBirthEdit_ != nullptr && !born.empty())
        dateOfBirthEdit_->setText(QString::fromStdString(born));
    return true;
}

void WorkflowWindow::showCaseDemographics(const beam::gui::Patient& patient) {
    if (lastNameEdit_ == nullptr) return;
    ui_->participantEdit->setText(QString::fromStdString(patient.externalId));
    ui_->siteEdit->setText(QString::fromStdString(patient.site));
    lastNameEdit_->setText(QString::fromStdString(patient.lastName));
    firstNameEdit_->setText(QString::fromStdString(patient.firstName));
    dateOfBirthEdit_->setText(QString::fromStdString(patient.dateOfBirth));
}

// Blank, or YYYY-MM-DD.
bool WorkflowWindow::readDateOfBirth(const QString& text, std::string* out) {
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        out->clear();
        return true;
    }
    // Leading zeros are not information, and slashes are accepted too.
    for (const QString& format : {QStringLiteral("yyyy-M-d"), QStringLiteral("yyyy/M/d"),
                                  QStringLiteral("yyyyMMdd")}) {
        const QDate parsed = QDate::fromString(trimmed, format);
        if (!parsed.isValid()) continue;
        *out = parsed.toString(QStringLiteral("yyyy-MM-dd")).toStdString();
        return true;
    }
    return false;
}

beam::gui::Patient WorkflowWindow::caseDemographics() const {
    beam::gui::Patient typed;
    typed.externalId = ui_->participantEdit->text().trimmed().toStdString();
    typed.site = ui_->siteEdit->text().trimmed().toStdString();
    if (lastNameEdit_ == nullptr) return typed;
    typed.lastName = lastNameEdit_->text().trimmed().toStdString();
    typed.firstName = firstNameEdit_->text().trimmed().toStdString();
    readDateOfBirth(dateOfBirthEdit_->text(), &typed.dateOfBirth);
    return typed;
}

// Who this device has treated, on the page where the operator says which of them is in front of
void WorkflowWindow::buildPatientChooser() {
    patientChooserGroup_ = new QGroupBox(QStringLiteral("Patients on this device"),
                                         ui_->casePage);
    auto* layout = new QVBoxLayout(patientChooserGroup_);
    layout->setContentsMargins(8, 6, 8, 8);
    layout->setSpacing(6);

    // A device that has treated two hundred people cannot be read down.
    auto* search = new QHBoxLayout;
    search->setSpacing(8);
    patientFilterEdit_ = new QLineEdit(patientChooserGroup_);
    patientFilterEdit_->setPlaceholderText(
        QStringLiteral("Find by Participant ID, name or site"));
    patientFilterEdit_->setClearButtonEnabled(true);
    styleInputField(patientFilterEdit_);
    connect(patientFilterEdit_, &QLineEdit::textChanged, this,
            [this] { refreshPatientChooser(); });
    search->addWidget(patientFilterEdit_, 1);
    patientCountLabel_ = new QLabel(patientChooserGroup_);
    patientCountLabel_->setStyleSheet(QStringLiteral("color: #7e8c93;"));
    search->addWidget(patientCountLabel_, 0);
    layout->addLayout(search);

    patientChooserHint_ = new QLabel(patientChooserGroup_);
    patientChooserHint_->setWordWrap(true);
    patientChooserHint_->setStyleSheet(QStringLiteral("color: #7e8c93;"));
    // Fixed, both of them: a QVBoxLayout hands spare height to whatever will take it, and with
    patientChooserHint_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    patientChooserGroup_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    layout->addWidget(patientChooserHint_);

    patientTable_ = new QTableWidget(0, 7, patientChooserGroup_);
    patientTable_->setHorizontalHeaderLabels({QStringLiteral("Participant ID"),
                                              QStringLiteral("Name"),
                                              QStringLiteral("Date of birth"),
                                              QStringLiteral("Site"),
                                              QStringLiteral("Visits"),
                                              QStringLiteral("Created"),
                                              QStringLiteral("Last treated")});
    patientTable_->verticalHeader()->setVisible(false);
    patientTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    patientTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    // Click a header to sort; the list opens in registry order, which is the order patients
    patientTable_->setSortingEnabled(true);
    patientTable_->horizontalHeader()->setSortIndicatorShown(true);
    // A deliberate opening order rather than whatever Qt last had.
    patientTable_->sortByColumn(0, Qt::AscendingOrder);
    patientTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    patientTable_->setAlternatingRowColors(true);
    patientTable_->setShowGrid(false);
    // Its own colours, not the registration table's: that one is styled for a widget with a
    patientTable_->setStyleSheet(QStringLiteral(
        "QTableWidget { color: #17313f; background: #ffffff; border: 1px solid #39758e; } "
        "QTableWidget::item { color: #17313f; background: #ffffff; padding: 3px 8px; } "
        "QTableWidget::item:alternate { color: #17313f; background: #eef4f7; } "
        "QTableWidget::item:selected, QTableWidget::item:selected:!active "
        "{ background: #58c7e5; color: #083d4d; }"));
    patientTable_->horizontalHeader()->setStyleSheet(QStringLiteral(
        "QHeaderView::section { padding: 3px 8px; margin: 0px; color: #17313f; "
        "background: #dce6ea; border: 0; border-bottom: 1px solid #39758e; }"));
    // Every column as wide as it needs to be, grouped at the left, with the last one absorbing
    for (int column = 0; column < patientTable_->columnCount(); ++column)
        patientTable_->horizontalHeader()->setSectionResizeMode(column,
                                                                QHeaderView::ResizeToContents);
    patientTable_->horizontalHeader()->setStretchLastSection(true);
    // Left, not Qt's centred default: the last column is the stretched one, so a centred label
    patientTable_->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    layout->addWidget(patientTable_);

    // Selecting fills the fields rather than accepting outright: the operator still presses
    const auto take = [this](int row) {
        beam::gui::Patient chosen;
        if (!patients_.find(patientIdAtRow(row), &chosen)) return;
        showCaseDemographics(chosen);
    };
    connect(patientTable_, &QTableWidget::currentCellChanged, this,
            [take](int row, int, int, int) { take(row); });
    connect(patientTable_, &QTableWidget::cellDoubleClicked, this,
            [this, take](int row, int) {
                take(row);
                ui_->acceptCaseButton->click();
            });

    patientTable_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(patientTable_, &QTableWidget::customContextMenuRequested, this,
            [this](const QPoint& where) {
                // Viewport coordinates, not the view's: the header is above the point this
                const QModelIndex at = patientTable_->indexAt(where);
                if (!at.isValid()) return;
                showPatientMenu(at.row(), patientTable_->viewport()->mapToGlobal(where));
            });

    // Under the identity form, above the spacer that holds the page open.
    ui_->casePageLayout->insertWidget(1, patientChooserGroup_, 0);
}

// The registry id carried on a row, or 0.
int WorkflowWindow::patientIdAtRow(int row) const {
    if (patientTable_ == nullptr || row < 0 || row >= patientTable_->rowCount()) return 0;
    const QTableWidgetItem* item = patientTable_->item(row, 0);
    return item == nullptr ? 0 : item->data(Qt::UserRole).toInt();
}

void WorkflowWindow::refreshPatientChooser() {
    if (patientTable_ == nullptr) return;
    const std::vector<beam::gui::Patient>& people = patients_.patients();
    patientChooserGroup_->setVisible(!people.empty());
    patientChooserHint_->setText(
        people.empty()
            ? QString()
            : QStringLiteral("Pick the patient in front of you, or use Create patient to start "
                             "a record. Each patient's visits are kept separately."));

    // Whoever the filter admits.
    const QString needle =
        patientFilterEdit_ == nullptr ? QString() : patientFilterEdit_->text().trimmed();
    std::vector<const beam::gui::Patient*> shown;
    for (const beam::gui::Patient& patient : people) {
        if (!needle.isEmpty()) {
            const QString haystack =
                QStringLiteral("%1 %2 %3 %4")
                    .arg(QString::fromStdString(patient.externalId),
                         QString::fromStdString(patient.lastName),
                         QString::fromStdString(patient.firstName),
                         QString::fromStdString(patient.site));
            if (!haystack.contains(needle, Qt::CaseInsensitive)) continue;
        }
        shown.push_back(&patient);
    }

    const QSignalBlocker blocker(patientTable_);
    // Sorting is off while the rows are written: with it on, each setItem can move the row
    patientTable_->setSortingEnabled(false);
    patientTable_->setRowCount(static_cast<int>(shown.size()));
    for (std::size_t index = 0; index < shown.size(); ++index) {
        const beam::gui::Patient& patient = *shown[index];
        // Counted in the store rather than by reading a record back out of it.
        int visits = 0;
        QString lastTreated;
        countPatientVisits(patient.id, &visits, &lastTreated);

        const auto cell = [](const QString& text, bool rightAlign = false) {
            auto* item = new QTableWidgetItem(text);
            if (rightAlign) item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            return item;
        };
        // Both stamps to the minute: the date alone cannot tell two visits on one day apart,
        const auto toMinute = [](const QString& stamp) {
            if (stamp.isEmpty()) return QStringLiteral("—");
            QString shown = stamp.left(16);
            shown.replace(QLatin1Char('T'), QLatin1Char(' '));
            return shown;
        };
        const int row = static_cast<int>(index);
        auto* identity = cell(QString::fromStdString(patient.externalId));
        // What the row IS, as against where it sits.
        identity->setData(Qt::UserRole, patient.id);
        patientTable_->setItem(row, 0, identity);
        patientTable_->setItem(
            row, 1,
            cell(patient.lastName.empty() && patient.firstName.empty()
                     ? QStringLiteral("—")
                     : QString::fromStdString(beam::gui::patientDisplayName(patient))));
        // Masked, not shown: the list is a lookup aid on a screen other people can see, and a
        patientTable_->setItem(row, 2,
                               cell(patient.dateOfBirth.empty()
                                        ? QStringLiteral("—")
                                        : QString::fromUtf8("\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2"
                                                            "\xe2\x80\xa2-\xe2\x80\xa2\xe2\x80\xa2"
                                                            "-\xe2\x80\xa2\xe2\x80\xa2")));
        patientTable_->setItem(row, 3, cell(QString::fromStdString(patient.site)));
        auto* visitCell = cell(QString::number(visits), true);
        // Sorted as a number, not as text, or 10 would come before 2.
        visitCell->setData(Qt::EditRole, visits);
        patientTable_->setItem(row, 4, visitCell);
        patientTable_->setItem(row, 5,
                               cell(toMinute(QString::fromStdString(patient.createdAt))));
        patientTable_->setItem(row, 6,
                               cell(lastTreated == QStringLiteral("unreadable")
                                        ? lastTreated
                                        : toMinute(lastTreated)));
        // The one being treated now, so the page says which record is open.
        if (patient.id == currentPatientId_) {
            for (int column = 0; column < patientTable_->columnCount(); ++column) {
                QTableWidgetItem* item = patientTable_->item(row, column);
                QFont bold = item->font();
                bold.setBold(true);
                item->setFont(bold);
            }
        }
    }
    patientTable_->setSortingEnabled(true);
    if (patientCountLabel_ != nullptr) {
        patientCountLabel_->setText(
            shown.size() == people.size()
                ? QStringLiteral("%1 patient(s)").arg(people.size())
                : QStringLiteral("%1 of %2 patient(s)").arg(shown.size()).arg(people.size()));
    }
    // As tall as its rows, capped: a device with many patients scrolls rather than pushing the
    const int rowHeight = patientTable_->verticalHeader()->defaultSectionSize();
    const int wanted = patientTable_->horizontalHeader()->height() +
                       static_cast<int>(shown.size()) * rowHeight +
                       2 * patientTable_->frameWidth();
    patientTable_->setFixedHeight(std::clamp(wanted, rowHeight * 2, rowHeight * 12));
}

// How many sonications this patient has actually had.
int WorkflowWindow::patientDeliveryCount(int patientId, int* plannedRows) const {
    int delivered = 0;
    if (plannedRows != nullptr) *plannedRows = 0;
    if (store_ == nullptr) return 1;
    try {
        beam::gui::TreatmentLog log;
        beam::gui::loadLog(*store_, patientId, &log);
        for (const beam::gui::TreatmentLogRow& row : log.rows()) {
            if (beam::gui::TreatmentLog::delivered(row)) ++delivered;
            else if (plannedRows != nullptr) ++*plannedRows;
        }
    } catch (const std::exception&) {
        // A record that cannot be read is not one that can be proved empty.
        return 1;
    }
    return delivered;
}

void WorkflowWindow::showPatientMenu(int row, const QPoint& globalPosition) {
    beam::gui::Patient patient;
    if (!patients_.find(patientIdAtRow(row), &patient)) return;
    const QString name = QString::fromStdString(patient.externalId);

    QMenu menu(this);
    QAction* edit = menu.addAction(QStringLiteral("Edit \"%1\" details…").arg(name));
    QAction* remove = menu.addAction(QStringLiteral("Delete \"%1\"").arg(name));

    int planned = 0;
    const int delivered = patientDeliveryCount(patient.id, &planned);
    const bool open = patient.id == currentPatientId_;
    remove->setEnabled(delivered == 0 && !open);
    if (open) {
        remove->setToolTip(QStringLiteral("This is the patient being treated. Open somebody "
                                          "else before deleting them."));
    } else if (delivered > 0) {
        remove->setToolTip(QStringLiteral("%1 has had %2 sonication(s). What a patient was "
                                          "given is a record and is not deleted here.")
                               .arg(name)
                               .arg(delivered));
    }
    // A disabled item says nothing on its own; the reason has to be reachable.
    menu.setToolTipsVisible(true);

    QAction* chosen = menu.exec(globalPosition);
    if (chosen == edit) editPatient(patient.id);
    else if (chosen == remove) deletePatient(patient.id, planned);
}

// Everything the operator can change about a patient, in one dialog.
void WorkflowWindow::editPatient(int patientId) {
    beam::gui::Patient patient;
    if (!patients_.find(patientId, &patient)) return;

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Patient details"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout;
    layout->addLayout(form);

    const auto field = [&](const QString& label, const std::string& value,
                           const QString& placeholder) {
        auto* edit = new QLineEdit(QString::fromStdString(value), &dialog);
        edit->setPlaceholderText(placeholder);
        styleInputField(edit);
        form->addRow(label, edit);
        return edit;
    };
    QLineEdit* identifier =
        field(QStringLiteral("Participant ID *"), patient.externalId, QStringLiteral("Required"));
    QLineEdit* site = field(QStringLiteral("Site ID"), patient.site, QString());
    QLineEdit* lastName =
        field(QStringLiteral("Last name"), patient.lastName, QStringLiteral("Optional"));
    QLineEdit* firstName =
        field(QStringLiteral("First name"), patient.firstName, QStringLiteral("Optional"));
    QLineEdit* born = field(QStringLiteral("Date of birth (YYYY-MM-DD)"), patient.dateOfBirth,
                            QStringLiteral("Optional"));

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel,
                                         &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    // Looped, so a field that will not do sends the operator back to the dialog with everything
    beam::gui::Patient edited;
    while (true) {
        if (dialog.exec() != QDialog::Accepted) return;

        edited = beam::gui::Patient{};
        edited.externalId = identifier->text().trimmed().toStdString();
        edited.site = site->text().trimmed().toStdString();
        edited.lastName = lastName->text().trimmed().toStdString();
        edited.firstName = firstName->text().trimmed().toStdString();
        if (!readDateOfBirth(born->text(), &edited.dateOfBirth)) {
            QMessageBox::warning(
                &dialog, QStringLiteral("Date of birth"),
                QStringLiteral("\"%1\" is not a date. Use YYYY-MM-DD, or leave it blank.")
                    .arg(born->text().trimmed()));
            born->setFocus(Qt::OtherFocusReason);
            born->selectAll();
            continue;
        }
        // Normalised, so the dialog shows what the record will hold.
        born->setText(QString::fromStdString(edited.dateOfBirth));
        if (!patients_.update(patientId, edited)) {
            QMessageBox::warning(
                &dialog, QStringLiteral("Participant ID"),
                QStringLiteral("\"%1\" could not be used: it is blank, or it already belongs "
                               "to another patient on this device.")
                    .arg(identifier->text().trimmed()));
            identifier->setFocus(Qt::OtherFocusReason);
            identifier->selectAll();
            continue;
        }
        break;
    }
    savePatientRegistry();
    // The open patient's log carries their identifier, and the Case page carries all five
    if (patientId == currentPatientId_) {
        treatmentLog_.setPatient(edited.externalId, edited.site,
                                 edited.lastName.empty() && edited.firstName.empty()
                                     ? std::string()
                                     : beam::gui::patientDisplayName(edited));
        beam::gui::Patient shown = edited;
        shown.id = patientId;
        showCaseDemographics(shown);
        saveTreatmentTargetStore(/*announce=*/false);
    }
    refreshPatientChooser();
    showMessage(QStringLiteral("Patient %1 saved as \"%2\" (%3).")
                    .arg(patientId)
                    .arg(QString::fromStdString(edited.externalId),
                         QString::fromStdString(beam::gui::patientDisplayName(edited))),
                false);
}

void WorkflowWindow::deletePatient(int patientId, int plannedRows) {
    beam::gui::Patient patient;
    if (!patients_.find(patientId, &patient)) return;
    const QString name = QString::fromStdString(patient.externalId);
    // Checked again here, not only when the menu was built: the menu may have been open while
    if (patientId == currentPatientId_ || patientDeliveryCount(patientId, nullptr) > 0) {
        showMessage(QStringLiteral("\"%1\" cannot be deleted.").arg(name), true);
        return;
    }

    QMessageBox ask(QMessageBox::Warning, QStringLiteral("Delete patient"),
                    QStringLiteral("Delete \"%1\" from this device?").arg(name),
                    QMessageBox::NoButton, this);
    ask.setInformativeText(
        plannedRows > 0
            ? QStringLiteral("They have had no sonications, but %1 planned row(s) will go with "
                             "them. This cannot be undone.")
                  .arg(plannedRows)
            : QStringLiteral("They have had no sonications. This cannot be undone."));
    QPushButton* confirm = ask.addButton(QStringLiteral("Delete"), QMessageBox::DestructiveRole);
    QPushButton* keep = ask.addButton(QStringLiteral("Keep"), QMessageBox::RejectRole);
    // Return on a delete dialog keeps them.
    ask.setDefaultButton(keep);
    ask.exec();
    if (ask.clickedButton() != confirm) return;

    // The directory goes with the row.
    const QDir runtime(treatmentDataDirectory());
    const QString directory = runtime.filePath(QStringLiteral("patients/%1").arg(patientId));
    const bool looksRight =
        QDir(directory).dirName() == QString::number(patientId) &&
        QFileInfo(QDir(directory).filePath(QStringLiteral(".."))).canonicalFilePath() ==
            QFileInfo(runtime.filePath(QStringLiteral("patients"))).canonicalFilePath();
    if (QFileInfo::exists(directory)) {
        // Refuse rather than carry on: dropping the registry row while the directory stays
        if (!looksRight || !QDir(directory).removeRecursively()) {
            showMessage(QStringLiteral("%1 could not be removed, so \"%2\" was left on the "
                                       "device rather than leaving their files behind.")
                            .arg(QDir::toNativeSeparators(directory), name),
                        true);
            return;
        }
    }
    patients_.remove(patientId);
    savePatientRegistry();
    refreshPatientChooser();
    showMessage(QStringLiteral("\"%1\" was deleted. Patient numbers are not reused, so nobody "
                               "else is given their place.")
                    .arg(name),
                false);
}

void WorkflowWindow::loadPatientRegistry() {
    readPatientsFromStore();
}

void WorkflowWindow::savePatientRegistry() {
    try {
        writeStore();
    } catch (const std::exception& error) {
        showMessage(QStringLiteral("The patient list could not be saved: %1").arg(error.what()),
                    true);
    }
}

// The Case page is where the operator says who is being treated, so accepting it is where that
void WorkflowWindow::selectPatient(const beam::gui::Patient& details, bool advancing) {
    const std::string identifier = details.externalId;
    if (identifier.empty()) return;

    beam::gui::Patient known;
    const bool onFile = patients_.findByExternalId(identifier, &known);
    if (onFile && known.id == currentPatientId_) {
        // The same person, re-accepted.
        showCaseDemographics(known);
        return;
    }
    const bool switching = currentPatientId_ > 0;
    // A sonication that is going out belongs to the patient it was started on.
    if (switching && deliveryRunning()) {
        // The header follows the field as it is typed, so leaving the new identifier in it
        beam::gui::Patient open;
        if (patients_.find(currentPatientId_, &open)) showCaseDemographics(open);
        showMessage(QStringLiteral("A sonication is still going out. Stop it before changing "
                                   "which patient is being treated."),
                    true);
        return;
    }
    // Whoever is open is written out before anybody else is read in, so a switch cannot carry
    if (switching) savePatientData();

    beam::gui::Patient patient;
    try {
        patient = patients_.add(details);
    } catch (const std::exception& error) {
        showMessage(QStringLiteral("This patient could not be recorded: %1").arg(error.what()),
                    true);
        return;
    }
    currentPatientId_ = patient.id;
    savePatientRegistry();
    // The record may hold details the Case page was not told, which is what picking an existing
    showCaseDemographics(patient);
    refreshPatientChooser();

    // Before the plan is rebuilt, not after: the rebuild reads the array position off the
    if (switching) {
        // Everything from the MRI down belongs to the person it was measured on.
        workflow_.change(beam::gui::WorkflowStage::Imaging,
                         "A different patient is being treated; this patient's imaging, "
                         "registration and coupling have not been done.");
        clearPatientImagingAndRegistration();
        clearCouplingMeasurement();
    }
    loadPatientData();
    if (switching) refresh();

    // Says what just happened and what the page it lands on is for; otherwise the jump to
    const QString who = QStringLiteral("%1 (%2)").arg(
        QString::fromStdString(beam::gui::patientDisplayName(patient)),
        QString::fromStdString(patient.externalId));
    const QString what =
        onFile ? QStringLiteral("Opened the existing record for %1 — patient %2.")
                     .arg(who)
                     .arg(patient.id)
               : QStringLiteral("Created a new record for %1 — patient %2 on this device.")
                     .arg(who)
                     .arg(patient.id);
    const QString next =
        advancing ? QStringLiteral(" Load their MRI to begin.")
                  : QStringLiteral(" They are in the list below. Accept the case when the "
                                   "record is right.");
    showMessage(what + next, false);
}

// A pre-patient install kept one subject's record in the runtime directory; the first patient

}  // namespace beam::app
