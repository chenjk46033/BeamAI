#pragma once

#include <QDialog>

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
    // One page at a time: targets are the shared template and protocols are
    // schedules over it, and a tabbed dialog holding both invited the two to
    // be read as one thing.
    enum class Page { Targets, Protocols };

    TreatmentLibraryDialog(beam::gui::TreatmentTargetStore store, Page page,
                           QWidget* parent = nullptr);

    const beam::gui::TreatmentTargetStore& store() const { return store_; }

private:
    void buildTargetsTab(QWidget* page);
    void buildProtocolsTab(QWidget* page);
    void reloadTargets(const QString& select = QString());
    void reloadProtocols(const QString& select = QString());
    void reloadEntries();
    void commitScheduleRow(int row);
    void commitScheduleOrder();
    // Which target's points the shared table is showing, and where they live:
    // the Target List's template on the Targets page, this protocol's own copy
    // on the Protocols page.
    QString pointsOwnerTarget() const;
    std::vector<beam::gui::TreatmentTargetPoint> currentPoints() const;
    bool writePoints(std::vector<beam::gui::TreatmentTargetPoint> points);
    void buildPointsSection(QWidget* page, QVBoxLayout* outer);
    void reloadTargetPoints();
    void commitTargetPoints();
    QString selectedTarget() const;
    QString selectedProtocol() const;

    Page page_ = Page::Targets;
    beam::gui::TreatmentTargetStore store_;
    QListWidget* targetList_ = nullptr;
    QComboBox* protocolCombo_ = nullptr;
    QLabel* scheduleCaption_ = nullptr;
    QTableWidget* entryTable_ = nullptr;
    QTableWidget* pointTable_ = nullptr;
    QLabel* pointCaption_ = nullptr;
    bool reloadingPoints_ = false;
    QComboBox* entryTargetCombo_ = nullptr;
};

}  // namespace beam::app
