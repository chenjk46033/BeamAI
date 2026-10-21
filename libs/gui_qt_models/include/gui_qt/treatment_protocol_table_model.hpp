#pragma once

#include <vector>

#include <QAbstractTableModel>
#include <QString>
#include <QStringList>

#include "gui/top_targets.hpp"

// A real Qt table model for BeamV0's GUI/SonicationTab
// `treatmentProtocolTable` (createTreatmentProtocolParamTable.m /
// setTreatmentProtocolTableNames.m): the per-sonication history a session
// logs pain/mood responses into, and the input
// getTopTargetsFromTreatmentProtocolTable.m ranks to decide what to carry
// forward. This model is what finally gives that already-ported algorithm
// (libs/gui/top_targets) a real caller.

namespace beam::gui_qt {

struct TreatmentProtocolRow {
    int number = 1;
    QString target = QStringLiteral("SCC");
    double duration = 30.0;
    double amplitude = 0.75;
    QString parameters = QStringLiteral("X:0,Y:0,Z:0,BD,BI,PD,PI");
    double responsePain = 0.0;
    double responseMood = 0.0;
    // When the sonication finished, as a time of day. Empty until it has.
    // When the delivery started and ended, to the millisecond, and how it
    // ended as an HTTP status. Shown as time of day; the full stamp with its
    // date is in the cell tooltip.
    QString startedAt;
    QString endedAt;
    QString startedAtFull;
    QString endedAtFull;
    int statusCode = 0;
    QString notes = QStringLiteral("notes");
    // Set once the sonication has been sent. A record of what happened is not
    // edited afterwards -- only what the operator observed about it.
    bool delivered = false;
    // What the safety rules say about this row as the plan stands -- empty
    // when it has nothing to say. Advisory only: the rules that actually
    // stop a delivery run against the sonication about to go out, and a row
    // further down can still change before it is reached.
    QStringList blockedBy;
    // A sham ran the countdown and the masking audio but sent no ultrasound,
    // which the log has to show as plainly as a real delivery.
    bool sham = false;
    // Stopped part-way by the operator: delivered, but not as planned.
    bool aborted = false;
};

class TreatmentProtocolTableModel : public QAbstractTableModel {
    Q_OBJECT

public:
    enum Column {
        // Empty in the model: the view puts a Sonicate button here on the row
        // the Current Sonication Number points at, so the row that fires next
        // carries the control that fires it.
        kAction = 0,
        kNumber,
        kTarget,
        kDuration,
        kAmplitude,
        kParameters,
        kResponsePain,
        kResponseMood,
        kStart,
        kEnd,
        kStatus,
        kNotes,
        kColumnCount
    };

    // True on the row the Current Sonication Number points at. The view's
    // delegate reads it to keep that row grey under the selection highlight.
    static constexpr int kCurrentRowRole = Qt::UserRole + 1;
    // The response colour alone, so a delegate can tell it from the plain
    // alternating fill and keep it visible under a selected row.
    static constexpr int kResponseFillRole = Qt::UserRole + 2;

    explicit TreatmentProtocolTableModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role) override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;

    void setRows(std::vector<TreatmentProtocolRow> rows);
    // setTreatmentProtocolTableData.m greys the row the Current Sonication
    // Number points at, so the operator can see where the visit has got to.
    // One-based to match that field; 0 greys nothing.
    void setCurrentSonication(int oneBased);
    const std::vector<TreatmentProtocolRow>& rows() const { return rows_; }

    // Runs beam::gui::getResponseFromTreatmentProtocolData over every row
    // (clamping ResponsePain/ResponseMood to [-2,2], matching the source's
    // own write-back of the clamped value into the table) and then
    // beam::gui::getTopTargetsFromTreatmentProtocolTable over the result --
    // the pure computation getTopTargetsFromTreatmentProtocolTable.m's
    // `app`-coupled preamble would otherwise assemble.
    beam::gui::BestTargets computeBestTargets(beam::gui::AccFlag accFlag);

signals:
    // Emitted whenever editing ResponsePain/ResponseMood clamps the stored
    // value (setCRFDateTime-style "something changed under the hood").
    void responseClamped();

private:
    std::vector<TreatmentProtocolRow> rows_;
    // 1-based; 0 means none.
    int currentSonication_ = 0;
};

}  // namespace beam::gui_qt
