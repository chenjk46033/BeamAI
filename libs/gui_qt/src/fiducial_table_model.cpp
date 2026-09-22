#include "gui_qt/fiducial_table_model.hpp"

namespace beam::gui_qt {

FiducialTableModel::FiducialTableModel(QObject* parent) : QAbstractTableModel(parent) {}

int FiducialTableModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(rows_.size());
}

int FiducialTableModel::columnCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(kColumnCount);
}

QVariant FiducialTableModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(rows_.size())) {
        return QVariant();
    }
    if (role != Qt::DisplayRole && role != Qt::EditRole) {
        return QVariant();
    }
    const FiducialRow& r = rows_[static_cast<std::size_t>(index.row())];
    switch (index.column()) {
        case kName: return r.name;
        case kX: return r.x;
        case kY: return r.y;
        case kZ: return r.z;
        case kGroup: return r.group;
        default: return QVariant();
    }
}

bool FiducialTableModel::setData(const QModelIndex& index, const QVariant& value, int role) {
    if (role != Qt::EditRole || !index.isValid() || index.row() < 0 ||
        index.row() >= static_cast<int>(rows_.size())) {
        return false;
    }
    FiducialRow& r = rows_[static_cast<std::size_t>(index.row())];
    switch (index.column()) {
        case kName: r.name = value.toString(); break;
        case kX: r.x = value.toDouble(); break;
        case kY: r.y = value.toDouble(); break;
        case kZ: r.z = value.toDouble(); break;
        case kGroup: r.group = value.toString(); break;
        default: return false;
    }
    emit dataChanged(index, index, {Qt::DisplayRole, Qt::EditRole});
    return true;
}

QVariant FiducialTableModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (role != Qt::DisplayRole) {
        return QVariant();
    }
    if (orientation == Qt::Vertical) {
        return section + 1;
    }
    switch (section) {
        case kName: return QStringLiteral("Name");
        case kX: return QStringLiteral("X (mm)");
        case kY: return QStringLiteral("Y (mm)");
        case kZ: return QStringLiteral("Z (mm)");
        case kGroup: return QStringLiteral("Group");
        default: return QVariant();
    }
}

Qt::ItemFlags FiducialTableModel::flags(const QModelIndex& index) const {
    if (!index.isValid()) {
        return Qt::NoItemFlags;
    }
    // Name/Group are read-only labels (identity, not editable data); X/Y/Z
    // are editable, standing in for BeamV0's draggable ROI positions until
    // there's a real MRI image view to drag on.
    Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (index.column() == kX || index.column() == kY || index.column() == kZ) {
        f |= Qt::ItemIsEditable;
    }
    return f;
}

void FiducialTableModel::setRows(std::vector<FiducialRow> rows) {
    beginResetModel();
    rows_ = std::move(rows);
    endResetModel();
}

}  // namespace beam::gui_qt
