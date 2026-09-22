#include "gui_qt/treatment_protocol_table_model.hpp"

#include <QColor>

#include "gui/sonication_tab_presenter.hpp"

namespace beam::gui_qt {

TreatmentProtocolTableModel::TreatmentProtocolTableModel(QObject* parent)
    : QAbstractTableModel(parent) {}

int TreatmentProtocolTableModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(rows_.size());
}

int TreatmentProtocolTableModel::columnCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(kColumnCount);
}

QVariant TreatmentProtocolTableModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(rows_.size())) {
        return QVariant();
    }
    const TreatmentProtocolRow& r = rows_[static_cast<std::size_t>(index.row())];

    if (role == Qt::BackgroundRole) {
        // setTreatmentProtocolTableData.m's per-row coloring, keyed off the
        // same clamped response computeBestTargets() feeds the ranking
        // algorithm. The source's separate "current sonication number"
        // grey override isn't wired here -- this model has no equivalent
        // concept yet.
        const beam::gui::MoodPainResponse resp =
            beam::gui::getResponseFromTreatmentProtocolData(r.responseMood, r.responsePain);
        const beam::gui::RowColor c = beam::gui::computeTreatmentRowColor(resp.total);
        // computeTreatmentRowColor's own "no response yet" case is flat
        // white for every row -- zebra-stripe just that case for
        // readability; a real clinical response color always wins.
        if (resp.total == 0.0) {
            return index.row() % 2 == 0 ? QColor(0xff, 0xff, 0xff) : QColor(0xf2, 0xf4, 0xf7);
        }
        return QColor::fromRgbF(c.r, c.g, c.b);
    }
    if (role != Qt::DisplayRole && role != Qt::EditRole) {
        return QVariant();
    }
    switch (index.column()) {
        case kNumber: return r.number;
        case kTarget: return r.target;
        case kDuration: return r.duration;
        case kAmplitude: return r.amplitude;
        case kParameters: return r.parameters;
        case kResponsePain: return r.responsePain;
        case kResponseMood: return r.responseMood;
        case kNotes: return r.notes;
        default: return QVariant();
    }
}

bool TreatmentProtocolTableModel::setData(const QModelIndex& index, const QVariant& value, int role) {
    if (role != Qt::EditRole || !index.isValid() || index.row() < 0 ||
        index.row() >= static_cast<int>(rows_.size())) {
        return false;
    }
    TreatmentProtocolRow& r = rows_[static_cast<std::size_t>(index.row())];
    bool clamped = false;

    switch (index.column()) {
        case kNumber:
            r.number = value.toInt();
            break;
        case kTarget:
            r.target = value.toString();
            break;
        case kDuration:
            r.duration = value.toDouble();
            break;
        case kAmplitude:
            r.amplitude = value.toDouble();
            break;
        case kParameters:
            r.parameters = value.toString();
            break;
        case kNotes:
            r.notes = value.toString();
            break;
        case kResponsePain: {
            r.responsePain = value.toDouble();
            const beam::gui::MoodPainResponse resp =
                beam::gui::getResponseFromTreatmentProtocolData(r.responseMood, r.responsePain);
            r.responsePain = resp.pain;  // source writes the clamped value back into the table
            clamped = true;
            break;
        }
        case kResponseMood: {
            r.responseMood = value.toDouble();
            const beam::gui::MoodPainResponse resp =
                beam::gui::getResponseFromTreatmentProtocolData(r.responseMood, r.responsePain);
            r.responseMood = resp.mood;
            clamped = true;
            break;
        }
        default:
            return false;
    }

    if (clamped) {
        // Pain/Mood together drive the whole row's background
        // (computeTreatmentRowColor) -- repaint the full row, not just the
        // edited cell.
        emit dataChanged(this->index(index.row(), 0), this->index(index.row(), kColumnCount - 1),
                         {Qt::DisplayRole, Qt::EditRole, Qt::BackgroundRole});
        emit responseClamped();
    } else {
        emit dataChanged(index, index, {Qt::DisplayRole, Qt::EditRole});
    }
    return true;
}

QVariant TreatmentProtocolTableModel::headerData(int section, Qt::Orientation orientation,
                                                 int role) const {
    if (role != Qt::DisplayRole) {
        return QVariant();
    }
    if (orientation == Qt::Vertical) {
        return section + 1;
    }
    // setTreatmentProtocolTableNames.m's renames.
    switch (section) {
        case kNumber: return QStringLiteral("#");
        case kTarget: return QStringLiteral("Target");
        case kDuration: return QStringLiteral("Duration (s)");
        case kAmplitude: return QStringLiteral("Amplitude");
        case kParameters: return QStringLiteral("Parameters");
        case kResponsePain: return QStringLiteral("Response Pain");
        case kResponseMood: return QStringLiteral("Response Mood/Anxiety");
        case kNotes: return QStringLiteral("Notes");
        default: return QVariant();
    }
}

Qt::ItemFlags TreatmentProtocolTableModel::flags(const QModelIndex& index) const {
    if (!index.isValid()) {
        return Qt::NoItemFlags;
    }
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable;
}

void TreatmentProtocolTableModel::setRows(std::vector<TreatmentProtocolRow> rows) {
    beginResetModel();
    rows_ = std::move(rows);
    endResetModel();
}

beam::gui::BestTargets TreatmentProtocolTableModel::computeBestTargets(beam::gui::AccFlag accFlag) {
    std::vector<beam::gui::TargetResponse> responses;
    responses.reserve(rows_.size());
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        const TreatmentProtocolRow& r = rows_[i];
        const beam::gui::MoodPainResponse resp =
            beam::gui::getResponseFromTreatmentProtocolData(r.responseMood, r.responsePain);
        beam::gui::TargetResponse tr;
        tr.sonicationNumber = static_cast<int>(i) + 1;
        tr.name = r.target.toStdString();
        tr.numericResponse = static_cast<double>(resp.total);
        tr.duration = r.duration;
        responses.push_back(tr);
    }
    return beam::gui::getTopTargetsFromTreatmentProtocolTable(responses, accFlag);
}

}  // namespace beam::gui_qt
