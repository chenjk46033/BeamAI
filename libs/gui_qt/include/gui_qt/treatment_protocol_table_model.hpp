#pragma once

#include <vector>

#include <QAbstractTableModel>
#include <QString>

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
    QString notes = QStringLiteral("notes");
};

class TreatmentProtocolTableModel : public QAbstractTableModel {
    Q_OBJECT

public:
    enum Column {
        kNumber = 0,
        kTarget,
        kDuration,
        kAmplitude,
        kParameters,
        kResponsePain,
        kResponseMood,
        kNotes,
        kColumnCount
    };

    explicit TreatmentProtocolTableModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role) override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;

    void setRows(std::vector<TreatmentProtocolRow> rows);
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
};

}  // namespace beam::gui_qt
