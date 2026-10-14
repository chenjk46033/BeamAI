#pragma once

#include <QDialog>

#include <functional>
#include <functional>
#include <utility>
#include <vector>

#include "gui/treatment_target_store.hpp"

QT_BEGIN_NAMESPACE
class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;
class QTableWidget;
class QVBoxLayout;
QT_END_NAMESPACE

namespace beam::app {

// Edits a copy; the caller takes store() only when the dialog is accepted.
class TreatmentLibraryDialog final : public QDialog {
public:
    // One page at a time: the Target List holds the targets themselves and
    // protocols are schedules over them, and a tabbed dialog holding both
    // invited the two to be read as one thing.
    enum class Page { Targets, Protocols };

    TreatmentLibraryDialog(beam::gui::TreatmentTargetStore store, Page page,
                           QWidget* parent = nullptr);

    const beam::gui::TreatmentTargetStore& store() const { return store_; }

    // Save writes without dismissing, so a run of edits is one visit.
    void setSaveHandler(std::function<void(const beam::gui::TreatmentTargetStore&)> handler);


private:
    void buildTargetsTab(QWidget* page);
    void buildProtocolsTab(QWidget* page);
    void reloadTargets(const QString& select = QString());
    void reloadProtocols(const QString& select = QString());
    void reloadEntries();
    void commitScheduleRow(int row);
    void commitScheduleOrder();
    // Which target's points the table is showing. They live on the target
    // itself; a protocol keeps none of its own.
    void commitTargetRow(int row);
    QString selectedTarget() const;
    QString selectedProtocol() const;

    std::function<void(const beam::gui::TreatmentTargetStore&)> saveHandler_;
    QLabel* savedNotice_ = nullptr;
    Page page_ = Page::Targets;
    beam::gui::TreatmentTargetStore store_;
    QTableWidget* targetTable_ = nullptr;
    std::vector<std::pair<std::size_t, std::size_t>> targetRows_;
    QComboBox* protocolCombo_ = nullptr;
    QTableWidget* entryTable_ = nullptr;
    bool reloadingPoints_ = false;
    QComboBox* entryTargetCombo_ = nullptr;
};

}  // namespace beam::app
