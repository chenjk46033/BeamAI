#pragma once

#include <QDialog>

#include "gui/treatment_target_store.hpp"

QT_BEGIN_NAMESPACE
class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;
class QTableWidget;
QT_END_NAMESPACE

namespace beam::app {

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
    void reloadTargetPoints();
    void commitTargetPoints();
    QString selectedTarget() const;
    QString selectedProtocol() const;

    beam::gui::TreatmentTargetStore store_;
    QListWidget* targetList_ = nullptr;
    QListWidget* protocolList_ = nullptr;
    QTableWidget* entryTable_ = nullptr;
    QTableWidget* pointTable_ = nullptr;
    QLabel* pointCaption_ = nullptr;
    bool reloadingPoints_ = false;
    QComboBox* entryTargetCombo_ = nullptr;
};

}  // namespace beam::app
