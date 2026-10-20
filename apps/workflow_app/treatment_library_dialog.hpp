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
class QHBoxLayout;
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

    // Says whose Target List is open, and offers the two directions between
    // it and the device-wide defaults.
    //
    // A target's parameters are prescribed for a person, so this dialog edits
    // one patient's list when a patient is open and the device's own when none
    // is. Those are very different acts behind one button, and nothing used to
    // distinguish them on screen.
    //
    // `whose` empty means the device-wide list is the one being edited;
    // `deviceDefaultsPath` is then ignored and no transfer buttons appear.
    void setTargetScope(const QString& whose, const QString& deviceDefaultsPath);


private:
    void buildTargetsTab(QWidget* page);
    void buildProtocolsTab(QWidget* page);
    void reloadTargets(const QString& select = QString());
    void reloadProtocols(const QString& select = QString());
    void reloadEntries();
    void commitScheduleRow(int row);
    // Moves one scheduled sonication to another position. The table is
    // rebuilt from the store afterwards, so the store is the only thing that
    // is ever reordered.
    void moveScheduleEntry(int from, int to);
    // Which target's points the table is showing. They live on the target
    // itself; a protocol keeps none of its own.
    void commitTargetRow(int row);
    // Shared by the Delete buttons and the right-click menus on both tables.
    void deleteTarget(const QString& name);
    void deleteScheduleEntry(int row);
    QString selectedTarget() const;
    QString selectedProtocol() const;

    std::function<void(const beam::gui::TreatmentTargetStore&)> saveHandler_;
    QLabel* savedNotice_ = nullptr;
    QLabel* scopeBanner_ = nullptr;
    QHBoxLayout* footerLayout_ = nullptr;
    QString deviceDefaultsPath_;
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
