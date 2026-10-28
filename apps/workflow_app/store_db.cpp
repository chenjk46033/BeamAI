#include "workflow_internal.hpp"

#include "gui/patient_registry_db.hpp"
#include "gui/treatment_log_db.hpp"
#include "gui/treatment_target_store_db.hpp"

namespace beam::app {

QString WorkflowWindow::storePath() const {
    const QString runtime = treatmentDataDirectory();
    if (runtime.isEmpty()) return QString();
    return QDir(runtime).filePath(QStringLiteral("beam.db"));
}

void WorkflowWindow::openStore() {
    const QString path = storePath();
    if (path.isEmpty()) return;
    try {
        store_ = std::make_unique<beam::db::Database>(path.toStdString());
        beam::db::applySchema(*store_);
    } catch (const std::exception& error) {
        store_.reset();
        showMessage(QStringLiteral("The treatment database could not be opened: %1")
                        .arg(error.what()),
                    true);
    }
}

void WorkflowWindow::readPatientsFromStore() {
    if (store_ == nullptr) return;
    try {
        beam::gui::loadPatients(*store_, &patients_);
    } catch (const std::exception& error) {
        showMessage(QStringLiteral("The patient list could not be read: %1").arg(error.what()),
                    true);
    }
}

// The open patient's own records. The shipped target list is still the seed for
// somebody who has none, which is install data rather than patient state.
void WorkflowWindow::readPatientDataFromStore() {
    if (store_ == nullptr || currentPatientId_ <= 0) return;
    try {
        beam::gui::loadLog(*store_, currentPatientId_, &treatmentLog_);
        beam::gui::TreatmentTargetStore stored;
        beam::gui::loadTargets(*store_, currentPatientId_, &stored);
        if (!stored.targets().empty()) targetStore_.adoptTargets(stored.targets());
    } catch (const std::exception& error) {
        showMessage(QStringLiteral("This patient's records could not be read: %1")
                        .arg(error.what()),
                    true);
    }
}

// How many visits this patient has, and when they were last treated. A query,
// not a record read back out and counted.
void WorkflowWindow::countPatientVisits(int patientId, int* visits,
                                        QString* lastTreated) const {
    if (visits != nullptr) *visits = 0;
    if (store_ == nullptr || patientId <= 0) return;
    try {
        auto count = store_->prepare(
            "SELECT COUNT(*), COALESCE(MAX(s.started_at), '') FROM visit v "
            "LEFT JOIN sonication s ON s.visit_id = v.id WHERE v.patient_id = ?");
        count.bind(1, static_cast<long long>(patientId));
        if (!count.step()) return;
        auto rows = store_->prepare("SELECT COUNT(*) FROM visit WHERE patient_id = ?");
        rows.bind(1, static_cast<long long>(patientId));
        if (rows.step() && visits != nullptr) *visits = static_cast<int>(rows.row().integer(0));
        if (lastTreated != nullptr)
            *lastTreated = QString::fromStdString(count.row().text(1));
    } catch (const std::exception&) {
        if (lastTreated != nullptr) *lastTreated = QStringLiteral("unreadable");
    }
}

// One transaction for everything this patient has, so a failure part way
// through leaves the store as it was rather than half written.
void WorkflowWindow::writeStore() {
    if (store_ == nullptr) return;
    try {
        beam::gui::savePatients(*store_, patients_);
        if (currentPatientId_ > 0) {
            beam::gui::saveLog(*store_, currentPatientId_, treatmentLog_);
            beam::gui::saveTargets(*store_, currentPatientId_, targetStore_);
        }
    } catch (const std::exception& error) {
        showMessage(QStringLiteral("The treatment database could not be written: %1")
                        .arg(error.what()),
                    true);
    }
}

}  // namespace beam::app
