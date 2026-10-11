#pragma once

#include <QDialog>

#include "gui/treatment_target_store.hpp"

QT_BEGIN_NAMESPACE
class QComboBox;
class QListWidget;
class QPushButton;
class QTableWidget;
QT_END_NAMESPACE

namespace beam::app {

// Editing the Target List and the protocols is configuration, shared across
// patients, not part of treating one. It happens here rather than in the
// Treatment plan body so a slip during a session cannot delete a target.
//
// Edits a copy; the caller takes store() only when the dialog is accepted.
class TreatmentLibraryDialog final : public QDialog {
public:
    TreatmentLibraryDialog(beam::gui::TreatmentTargetStore store, QWidget* parent = nullptr);

    const beam::gui::TreatmentTargetStore& store() const { return store_; }

private:
    void buildTargetsTab(QWidget* page);
    void buildProtocolsTab(QWidget* page);
    void reloadTargets(const QString& select = QString());
    void reloadProtocols(const QString& select = QString());
    void reloadEntries();
    QString selectedTarget() const;
    QString selectedProtocol() const;

    beam::gui::TreatmentTargetStore store_;
    QListWidget* targetList_ = nullptr;
    QListWidget* protocolList_ = nullptr;
    QTableWidget* entryTable_ = nullptr;
    QComboBox* entryTargetCombo_ = nullptr;
};

}  // namespace beam::app
