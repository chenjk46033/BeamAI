#pragma once

#include <functional>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

#include <QMainWindow>
#include <QRect>
#include <QSize>
#include <QString>
#include <Eigen/Core>

#include "gui/registration_check_presenter.hpp"
#include "gui/safety_presenter.hpp"
#include "gui_qt/stim_param_table_model.hpp"
#include "mri/ras_transform.hpp"
#include "mri/slice.hpp"

class QListWidgetItem;
class QResizeEvent;
class QTimer;
class QVBoxLayout;

namespace beam::gui_qt {
class TotalSonicationView;
class PulseWaveformView;
}

// A second, parallel BeamV0 startup-screen implementation, built from
// designer/startup_screen.ui (a Qt Designer XML file, hand-converted from
// BeamV0.mlapp's own createComponents layout) instead of BeamMainWindow's
// hand-built C++ widget construction -- a test of whether the .ui-file
// workflow can drive real, working behavior, not just static layout.
//
// Deliberately scoped to the "persistent chrome" only: the 3 MRI slice
// views, the overlay checkboxes, System Status, and the CRF fields -- the
// app.UIFigure-parented controls confirmed visible on every tab, not the
// deeper Sonicate-tab-specific grids/models (stimParamTable, target list,
// serial connect, treatment protocol, etc.) BeamMainWindow also wires.
// Those widgets are still present and laid out in the .ui (real, not
// invented), just not connected to any handler here -- clicking them does
// nothing in this window.
//
// Shares the exact same domain/presenter calls (libs/gui, libs/mri) and
// the exact same MriSliceView class BeamMainWindow uses -- no new
// business logic, only a different widget-construction layer
// underneath. The safety report is the one exception: BeamMainWindow's
// SafetyReportView shows a status line plus the full message list at
// all times, but this window's own top-row layout only has room for
// one line (user: "move the message box to the right of the top row,
// showing the current line (first line). but when mouse over, it will
// show entire current content."), a display mode SafetyReportView
// doesn't have -- so this window drives ui_->SystemStatusTextArea
// directly instead of using that shared composite widget.

namespace Ui {
class BeamStartupScreen;
}

namespace beam::gui_qt {

class MriSliceView;
class TreatmentProtocolTableModel;
class ZoomableImageView;

class DesignerStartupWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit DesignerStartupWindow(QWidget* parent = nullptr);
    ~DesignerStartupWindow() override;

    // See BeamMainWindow's identically-named methods for what each of
    // these does -- same real behavior, just applied to this window's own
    // (Designer-built) MRI views/safety view/CRF fields.
    void setSafetyReport(const beam::gui::SonicationSafetyReport& report);
    void setMriVolume(const beam::mri::Volume3D& volume);
    void setMriAxes(std::optional<beam::mri::RasAxisVectors> axes);
    void setMriOverlays(const beam::mri::Volume3D& arrayMask, const beam::mri::Volume3D& fiducialMask,
                        const beam::mri::Volume3D& focusMask);
    void setArrayCenterMm(const Eigen::Vector3d& centerArrayMm);
    void centerSagittalSliceOnMm(double xMm);
    void setLoadMriHandler(std::function<void(QString path)> onLoadMri);
    void setFiducialMarkers(std::vector<std::pair<QString, Eigen::Vector3d>> markersMm);

    // See BeamMainWindow's identically-named methods for what each of
    // these does -- same real behavior, applied to this window's own
    // (Designer-built) Register-tab widgets: the Left/Right Horizontal/
    // VerticalPosition sliders, RegisterToMRIFiducialsButton,
    // RegisterArraystoCurrentPositionButton, and the 3 registration lamps
    // + SonicateButton's enable state.
    void setRegisterHandler(std::function<void()> onRegister);
    void setRegisterCurrentPositionHandler(std::function<void(double horizontalValue, double verticalValue)>
                                                onRegisterCurrentPosition);
    void setPositionSlidersChangedHandler(std::function<void()> onPositionSlidersChanged);
    void setRegistrationCheckLampState(const beam::gui::RegistrationCheckLampState& state);

    QString siteId() const;
    QString visitNumber() const;
    QString participantId() const;
    void setSiteId(const QString& value);
    void setParticipantId(const QString& value);
    void setVisitNumberField(const QString& value);

    // See BeamMainWindow's identically-named methods for what each of
    // these does -- same real behavior, applied to this window's own
    // (Designer-built) Sonicate-tab widgets: stimParamTable/
    // SonicationStatusLabel/SonicationCountdownLabel, Sonicate/Sham/
    // GetParams/Abort buttons, and SerialPortDropDown/SerialConnectButton/
    // ConnectedLamp.
    // Pulse waveform / burst waveform / total sonication timeline charts
    // (axPulseWaveformPlot/axBurstWaveformPlot/axTotalSonicationPlot),
    // rebuilt automatically for whichever row is currently "shown"
    // whenever the model changes -- see refreshCharts().
    void setStimParamTableModel(StimParamTableModel* model);
    void setSonicateHandler(std::function<void(bool waitForExternalTrigger)> onSonicate);
    void setSonicationStatus(const QString& text);
    void setShamHandler(std::function<void()> onSham);
    void setSerialPorts(const std::vector<QString>& ports);
    void setSerialConnectHandler(std::function<void(QString)> onSerialConnect);
    void setSerialConnectedState(bool connected);
    void setGetParamsHandler(std::function<void()> onGetParams);
    void setAbortSonicationHandler(std::function<void()> onAbortSonication);
    void startSonicationCountdown(int durationSeconds);
    QString sonicationCountdownText() const;

    // See BeamMainWindow's identically-named methods for what each of
    // these does -- same real behavior, applied to this window's own
    // (Designer-built) Treatment Protocol sub-tab widgets. VisitNumberListBox
    // is a real QListWidget here (matching BeamV0.mlapp's own widget type),
    // not BeamMainWindow's own simplified QComboBox -- setVisitNumbers/
    // currentVisitNumber are adapted accordingly, same external contract.
    void setTreatmentProtocolTableModel(TreatmentProtocolTableModel* model);
    void setTreatmentProtocolNames(const std::vector<QString>& names);
    void setVisitNumbers(const std::vector<int>& visitNumbers, int currentVisitNumber);
    void setTreatmentProtocolSelectorHandler(std::function<void(const QString&, int)> onSelectionChanged);
    void setNewVisitHandler(std::function<void()> onNewVisit);
    QString currentTreatmentProtocolName() const;
    int currentVisitNumber() const;
    void setTreatmentProtocolDataChangedHandler(std::function<void()> onDataChanged);
    QString hydrogelSize() const;  // "Small" / "Medium" / "Large"
    int currentSonicationNumber() const;

protected:
    // The whole .ui is absolute-positioned (a literal BeamV0.mlapp
    // conversion, deliberately -- see this class's own header comment),
    // so resizing this top-level window doesn't move or grow a single
    // one of its children on its own the way a real Qt layout would
    // (user: "your Beam app does not zoom correctly. It simply does not
    // zoom, when windows resize bigger"). Rescales every absolutely-
    // positioned descendant proportionally instead, mirroring MATLAB App
    // Designer's own "Scalable" resize behavior -- see
    // captureBaselineGeometry()'s own comment for exactly which widgets
    // that does and doesn't include.
    void resizeEvent(QResizeEvent* event) override;

private:
    void applyOverlayVisibility();
    // Now the real thing, not a simplification -- BeamMainWindow's own
    // refreshTargetCrosshairs, checking every stimParamModel_ row's
    // "show" flag once a real model exists (setStimParamTableModel).
    void refreshTargetCrosshairs();
    // Rebuilds the pulse/burst/total-sonication charts for whichever row
    // is currently "shown" -- same trigger points as refreshTargetCrosshairs.
    void refreshCharts();
    void loadMriFile();
    // Target List's real per-target row storage (BeamV0's own
    // app.sys.protocolTables(i).stimParamTableData -- switching the
    // selected list item swaps stimParamTable to *that* target's own,
    // independently-stored rows, not one shared table for every target;
    // user: "when scc1, there should be just one row, but you show
    // three rows"). Indexed by TargetListListBox row, not by name, so
    // Rename can't desync a lookup -- see targetRowsStore_'s own
    // comment. Writes the current stimParamModel_ rows back into
    // targetRowsStore_[index] before switching away from it.
    void loadTargetRow(int index);
    // Builds a TargetListListBox row's icon buttons.
    void installTargetRowButtons(QListWidgetItem* item);
    // Tints the current row since item widgets hide native selection.
    void updateTargetListHighlight();
    // Records every absolutely-positioned descendant's own .ui-designed
    // geometry once, right after all of this constructor's own layout
    // fixups (MRI-view height reclaim, .ui-added Sonicate-tab widgets,
    // etc.) -- resizeEvent()'s baseline, not the raw .ui XML values.
    // Stops recursing at MriSliceView (real Qt layout, own working
    // resizeEvent -- rescaling only its outer rect here, one level up
    // in the call that reaches it, is correct; going further would
    // double-transform its internal spacers/labels on top of its own
    // layout's response) and at QAbstractScrollArea (QTableView/
    // QListWidget/QTextEdit -- manually setGeometry()-ing their
    // internal qt_scrollarea_viewport/h|vcontainer children fights
    // their own internal layout for those, corrupting state that
    // crashed later on repaint, not immediately, the first time this
    // recursed into them). Also skips recording/setting geometry for a
    // QTabWidget's own pages specifically (SonicateTab, RegisterTab,
    // etc.) -- Qt parents those under the QTabWidget's own internal
    // QStackedWidget, which auto-fills its current page to its own
    // size on every layout pass; fighting that the same way as the
    // scroll-area case above corrupted state and crashed on repaint.
    // Still recurses into each page so its own absolutely-positioned
    // children keep getting captured and scaled.
    void captureBaselineGeometry(QWidget* widget);

    Ui::BeamStartupScreen* ui_;
    MriSliceView* sagitalView_;
    MriSliceView* coronalView_;
    MriSliceView* axialView_;
    ZoomableImageView* exampleSagView_;
    ZoomableImageView* exampleCorView_;
    ZoomableImageView* exampleAxialView_;

    std::function<void(QString)> onLoadMri_;
    Eigen::Vector3d arrayCenterMm_ = Eigen::Vector3d::Zero();
    beam::mri::Volume3D lastArrayMask_;
    beam::mri::Volume3D lastFiducialMask_;
    beam::mri::Volume3D lastFocusMask_;

    StimParamTableModel* stimParamModel_ = nullptr;
    // See BeamMainWindow's identically-named members' own comments.
    QTimer* countdownTimer_;
    int countdownDurationSeconds_ = 0;
    int countdownTickCount_ = 0;

    // Parallel to TargetListListBox's own items (same order/length),
    // one row set per target -- seeded with a single default-constructed
    // StimParamRow{} per target (its own default member initializers
    // already match createStimParamTable.m's real defaults: Amplitude
    // 0.5, startTime 0, endTime 30, BD 0.03, BI 0.7, PD 0.005, PI 0.01).
    // Index-based, not name-based, so RenameButton can't orphan a
    // target's stored data by changing its key.
    std::vector<std::vector<StimParamRow>> targetRowsStore_;
    // Set right before loadTargetRow() writes the outgoing target's
    // edits back and swaps in the new one -- -1 means "nothing loaded
    // yet" (stimParamModel_ itself is still nullptr at that point).
    int currentTargetIndex_ = -1;

    // See captureBaselineGeometry()'s own comment. baselineCentralSize_
    // empty means "not captured yet" (guards the very first, pre-
    // baseline resizeEvent that setupUi()/show() themselves trigger).
    QSize baselineCentralSize_;
    std::unordered_map<QWidget*, QRect> baselineGeometry_;

    // The 3 chart placeholders (axPulseWaveformPlot/axBurstWaveformPlot/
    // axTotalSonicationPlot) -- see refreshCharts(). All 3 are
    // hand-painted (PulseWaveformView/TotalSonicationView) rather than
    // QtCharts QChartViews: QtCharts renders garbled "..." tick labels
    // for Total Sonication's real burst-timeline data at any size (root
    // cause never found despite extensive testing), and renders nothing
    // but the bare title for Pulse/Burst at the short height needed to
    // fit all 3 charts on screen without scrolling -- painting directly
    // sidesteps both failure modes, since there's no "automatic" step to
    // fail.
    PulseWaveformView* pulseWaveformView_;
    PulseWaveformView* burstWaveformView_;
    TotalSonicationView* totalSonicationView_;
};

}  // namespace beam::gui_qt
