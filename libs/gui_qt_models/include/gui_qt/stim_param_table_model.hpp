#pragma once

#include <vector>

#include <QAbstractTableModel>

// A real Qt table model for BeamV0's GUI/SonicationTab `stimParamTable`
// (createStimParamTable.m / setStimParamTableNames.m) -- the central
// per-target sonication-parameter grid the whole Sonicate tab (and several
// already-ported presenters: getCurrentShownSonication,
// sortSonicationTableOrder, computePulseWaveformPlot) operate on.
//
// Column order and headers match the source's `table(order, show, X, Y, Z,
// Amplitude, startTime, endTime, BD, BI, PD, PI)` + setStimParamTableNames.m's
// renames. The source's later-appended BLKD/BLKI/Block columns (referenced
// by setStimParamTableNames.m's ColumnWidth{14}/{15} but not present in
// createStimParamTable.m's own `table(...)` call) are not modeled here --
// not traced to whichever caller adds them.

namespace beam::gui_qt {

struct StimParamRow {
    int order = 1;
    bool show = true;
    double x = 0.0, y = 0.0, z = 0.0;
    double amplitude = 0.5;
    double startTime = 0.0, endTime = 30.0;
    double bd = 0.03, bi = 0.7, pd = 0.005, pi = 0.01;
};

class StimParamTableModel : public QAbstractTableModel {
    Q_OBJECT

public:
    enum Column {
        kOrder = 0,
        kShow,
        kX,
        kY,
        kZ,
        kAmplitude,
        kStartTime,
        kEndTime,
        kBurstDuration,
        kBurstInterval,
        kPulseDuration,
        kPulseInterval,
        kColumnCount
    };

    explicit StimParamTableModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role) override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;

    void setRows(std::vector<StimParamRow> rows);
    const std::vector<StimParamRow>& rows() const { return rows_; }

    // Reorders rows via beam::gui::sortSonicationTableOrder (sortSonicationTable.m)
    // over the current "#"/order column.
    void sortByOrderColumn();

    // beam::gui::getCurrentShownSonication (getCurrentShownSonication.m)
    // over the current Show column. 1-based, matching the presenter.
    int currentShownRow() const;

    // Port of createSonicationUpdateTable.m's live path (the rest of the
    // source is commented out): duplicates the last row and renumbers its
    // "#" to the new row count. Throws std::logic_error if there are no
    // rows yet -- the source has the same implicit assumption
    // (`t(order-1,:)` with `order=1` would index row 0, which MATLAB
    // itself would error on).
    void addSonicationRow();

    // Port of removeSonicationUpdateTable.m. Faithful-port quirk,
    // preserved exactly: the source force-clears row 1's Show flag first
    // (`data(1,2) = 0`), *then* deletes every row whose Show flag is set
    // (`i = data(:,2)==1; t(i,:)=[]`) -- so "Show" doubles as the
    // deletion marker here, and row 1 can never be removed by this
    // operation.
    void removeMarkedRows();

signals:
    // Emitted whenever a Show checkbox is toggled, so the caller can
    // recompute the pulse-waveform plot for the (possibly new)
    // currentShownRow().
    void showFlagsChanged();

private:
    std::vector<StimParamRow> rows_;
};

}  // namespace beam::gui_qt
