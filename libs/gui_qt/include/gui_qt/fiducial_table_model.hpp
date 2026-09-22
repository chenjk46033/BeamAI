#pragma once

#include <vector>

#include <QAbstractTableModel>
#include <QString>

// A real Qt table model for BeamV0's GUI/RegistrationTab
// `fiducialMarkerTable` / `app.FiducialROIs`, as populated by
// `setFiducialROIs.m`. Register tab's first real content, replacing its
// placeholder.

namespace beam::gui_qt {

struct FiducialRow {
    QString name;
    double x = 0.0, y = 0.0, z = 0.0;  // mm, matching the source's *1000 scaling
    QString group = QStringLiteral("Transducer");
};

class FiducialTableModel : public QAbstractTableModel {
    Q_OBJECT

public:
    enum Column { kName = 0, kX, kY, kZ, kGroup, kColumnCount };

    explicit FiducialTableModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role) override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;

    void setRows(std::vector<FiducialRow> rows);
    const std::vector<FiducialRow>& rows() const { return rows_; }

private:
    std::vector<FiducialRow> rows_;
};

}  // namespace beam::gui_qt
