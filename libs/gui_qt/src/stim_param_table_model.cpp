#include "gui_qt/stim_param_table_model.hpp"

#include <algorithm>
#include <stdexcept>

#include "gui/sonication_tab_presenter.hpp"

namespace beam::gui_qt {

namespace {

double* editableField(StimParamRow& row, int column) {
    switch (column) {
        case StimParamTableModel::kOrder:
            return nullptr;  // int, handled separately
        case StimParamTableModel::kX:
            return &row.x;
        case StimParamTableModel::kY:
            return &row.y;
        case StimParamTableModel::kZ:
            return &row.z;
        case StimParamTableModel::kAmplitude:
            return &row.amplitude;
        case StimParamTableModel::kStartTime:
            return &row.startTime;
        case StimParamTableModel::kEndTime:
            return &row.endTime;
        case StimParamTableModel::kBurstDuration:
            return &row.bd;
        case StimParamTableModel::kBurstInterval:
            return &row.bi;
        case StimParamTableModel::kPulseDuration:
            return &row.pd;
        case StimParamTableModel::kPulseInterval:
            return &row.pi;
        default:
            return nullptr;
    }
}

}  // namespace

StimParamTableModel::StimParamTableModel(QObject* parent) : QAbstractTableModel(parent) {}

int StimParamTableModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(rows_.size());
}

int StimParamTableModel::columnCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(kColumnCount);
}

QVariant StimParamTableModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(rows_.size())) {
        return QVariant();
    }
    const StimParamRow& r = rows_[static_cast<std::size_t>(index.row())];

    if (role == Qt::CheckStateRole && index.column() == kShow) {
        return r.show ? Qt::Checked : Qt::Unchecked;
    }
    if (role == Qt::TextAlignmentRole && index.column() == kShow) {
        return QVariant(Qt::AlignCenter);
    }
    if (role != Qt::DisplayRole && role != Qt::EditRole) {
        return QVariant();
    }
    switch (index.column()) {
        case kOrder: return r.order;
        case kShow: return QVariant();  // rendered via CheckStateRole only
        case kX: return r.x;
        case kY: return r.y;
        case kZ: return r.z;
        case kAmplitude: return r.amplitude;
        case kStartTime: return r.startTime;
        case kEndTime: return r.endTime;
        case kBurstDuration: return r.bd;
        case kBurstInterval: return r.bi;
        case kPulseDuration: return r.pd;
        case kPulseInterval: return r.pi;
        default: return QVariant();
    }
}

bool StimParamTableModel::setData(const QModelIndex& index, const QVariant& value, int role) {
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(rows_.size())) {
        return false;
    }
    StimParamRow& r = rows_[static_cast<std::size_t>(index.row())];

    if (role == Qt::CheckStateRole && index.column() == kShow) {
        r.show = (value.toInt() == Qt::Checked);
        emit dataChanged(index, index, {Qt::CheckStateRole});
        emit showFlagsChanged();
        return true;
    }
    if (role != Qt::EditRole) {
        return false;
    }
    if (index.column() == kOrder) {
        r.order = value.toInt();
    } else if (double* field = editableField(r, index.column())) {
        *field = value.toDouble();
    } else {
        return false;
    }
    emit dataChanged(index, index, {Qt::DisplayRole, Qt::EditRole});
    return true;
}

QVariant StimParamTableModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (role != Qt::DisplayRole) {
        return QVariant();
    }
    if (orientation == Qt::Vertical) {
        return section + 1;
    }
    // setStimParamTableNames.m's renames, plus the table()-default names for
    // the columns it doesn't override.
    switch (section) {
        case kOrder: return QStringLiteral("#");
        case kShow: return QStringLiteral("Show");
        case kX: return QStringLiteral("X");
        case kY: return QStringLiteral("Y");
        case kZ: return QStringLiteral("Z");
        case kAmplitude: return QStringLiteral("Amplitude");
        case kStartTime: return QStringLiteral("Start Time");
        case kEndTime: return QStringLiteral("End Time");
        case kBurstDuration: return QStringLiteral("Burst Duration");
        case kBurstInterval: return QStringLiteral("Burst Interval");
        case kPulseDuration: return QStringLiteral("Pulse Duration");
        case kPulseInterval: return QStringLiteral("Pulse Interval");
        default: return QVariant();
    }
}

Qt::ItemFlags StimParamTableModel::flags(const QModelIndex& index) const {
    if (!index.isValid()) {
        return Qt::NoItemFlags;
    }
    Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (index.column() == kShow) {
        f |= Qt::ItemIsUserCheckable;
    } else {
        f |= Qt::ItemIsEditable;
    }
    return f;
}

void StimParamTableModel::setRows(std::vector<StimParamRow> rows) {
    beginResetModel();
    rows_ = std::move(rows);
    endResetModel();
}

void StimParamTableModel::sortByOrderColumn() {
    std::vector<double> order;
    order.reserve(rows_.size());
    for (const StimParamRow& r : rows_) {
        order.push_back(static_cast<double>(r.order));
    }
    const std::vector<int> perm = beam::gui::sortSonicationTableOrder(order);

    std::vector<StimParamRow> sorted;
    sorted.reserve(rows_.size());
    for (int idx : perm) {
        sorted.push_back(rows_[static_cast<std::size_t>(idx)]);
    }

    beginResetModel();
    rows_ = std::move(sorted);
    endResetModel();
}

int StimParamTableModel::currentShownRow() const {
    std::vector<bool> show;
    show.reserve(rows_.size());
    for (const StimParamRow& r : rows_) {
        show.push_back(r.show);
    }
    return beam::gui::getCurrentShownSonication(show);
}

void StimParamTableModel::addSonicationRow() {
    if (rows_.empty()) {
        throw std::logic_error("StimParamTableModel::addSonicationRow: no existing row to duplicate");
    }
    beginInsertRows(QModelIndex(), rowCount(), rowCount());
    StimParamRow newRow = rows_.back();  // t(order,:) = t(order-1,:)
    newRow.order = static_cast<int>(rows_.size()) + 1;  // t(order,1) = table(order)
    rows_.push_back(newRow);
    endInsertRows();
}

void StimParamTableModel::removeMarkedRows() {
    if (rows_.empty()) {
        return;
    }
    // Faithful-port quirk (see header): row 1 is force-cleared first, so it
    // can never be removed by this operation; every remaining row with
    // Show set is then deleted.
    rows_.front().show = false;

    beginResetModel();
    rows_.erase(std::remove_if(rows_.begin(), rows_.end(), [](const StimParamRow& r) { return r.show; }),
               rows_.end());
    endResetModel();
    emit showFlagsChanged();
}

}  // namespace beam::gui_qt
