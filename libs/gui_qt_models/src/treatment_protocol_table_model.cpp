#include "gui_qt/treatment_protocol_table_model.hpp"

#include <QColor>
#include <QIcon>
#include <QPainter>
#include <QPixmap>

#include "gui/sonication_tab_presenter.hpp"

namespace beam::gui_qt {

TreatmentProtocolTableModel::TreatmentProtocolTableModel(QObject* parent)
    : QAbstractTableModel(parent) {}

int TreatmentProtocolTableModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    // A visit with nothing in it draws one blank row. A table with a header
    // and no body reads as broken; a blank row reads as somewhere to put
    // something, which is what a right-click there does. It is not a row of
    // anything: rows() stays empty, so nothing counts it or fires it.
    return rows_.empty() ? 1 : static_cast<int>(rows_.size());
}

int TreatmentProtocolTableModel::columnCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(kColumnCount);
}

namespace {

QIcon makeTick(const QColor& colour, bool cutShort) {
    const int side = 16;
    QPixmap canvas(side, side);
    canvas.fill(Qt::transparent);
    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::Antialiasing, true);
    // A stopped sonication keeps the tick -- it ran -- but in grey, so the
    // red slash across it is what the eye lands on.
    QPen stroke(cutShort ? QColor(0x9a, 0xa7, 0xad) : colour, 2.6);
    stroke.setCapStyle(Qt::RoundCap);
    stroke.setJoinStyle(Qt::RoundJoin);
    painter.setPen(stroke);
    QPolygonF check;
    check << QPointF(3.5, 8.5) << QPointF(6.6, 11.8) << QPointF(12.5, 4.6);
    painter.drawPolyline(check);
    if (cutShort) {
        // Struck through: the sonication happened, but not all of it. The
        // tick stays underneath because energy did go out -- the row is a
        // record, not a plan that was abandoned.
        // The slash is red and crosses the whole icon, and the tick under it
        // is drawn in grey rather than the colour it would have had. Colour
        // does the work: a short slash in the tick's own colour was a
        // difference you had to look for, and in a column of ticks nobody
        // looks that hard.
        QPen bar(QColor(0xff, 0x55, 0x4a), 2.6);
        bar.setCapStyle(Qt::RoundCap);
        painter.setPen(bar);
        painter.drawLine(QPointF(2.6, 2.6), QPointF(13.4, 13.4));
    }
    return QIcon(canvas);
}

// setTreatmentProtocolTableData.m: yellow below zero, and above it a green
// that deepens with the score -- [0, 0.5 + 0.5*(min(r,4)/4), 0].
QColor responseFill(int response) {
    if (response < 0) return QColor(0xff, 0xff, 0x00);
    if (response == 0) return QColor();
    const double strength = 0.5 + 0.5 * (std::min(response, 4) / 4.0);
    return QColor(0, static_cast<int>(std::lround(255.0 * strength)), 0);
}

// A delivered sonication shows a tick where the one still to run shows its
// Sonicate button -- the column says, row by row, what has happened and what
// is about to. A tick rather than a cross: nothing here failed.
//
// Green for a real delivery, amber for a sham, matching the progress bar that
// ran while each was going out. The two are a hue apart and nothing else, so
// the tooltip says which as well -- that pair is the one hardest to tell
// apart for a red-green colour-blind reader.
QIcon deliveredTick(bool sham, bool aborted) {
    static const QIcon real = makeTick(QColor(0x5e, 0xc4, 0x6a), false);    // light green
    static const QIcon masked = makeTick(QColor(0xe0, 0xa3, 0x2e), false);  // amber
    // Both stopped icons are the same grey tick under the same red slash:
    // what matters about the row is that it was cut short.
    static const QIcon realCut = makeTick(QColor(0x5e, 0xc4, 0x6a), true);
    static const QIcon maskedCut = makeTick(QColor(0xe0, 0xa3, 0x2e), true);
    if (aborted) return sham ? maskedCut : realCut;
    return sham ? masked : real;
}

}  // namespace

QVariant TreatmentProtocolTableModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(rows_.size())) {
        return QVariant();
    }
    const TreatmentProtocolRow& r = rows_[static_cast<std::size_t>(index.row())];

    const bool current = currentSonication_ > 0 && index.row() == currentSonication_ - 1;
    if (role == kCurrentRowRole) return current;
    if (role == kResponseFillRole) {
        if (!r.delivered) return QVariant();
        const QColor fill = responseFill(
            beam::gui::getResponseFromTreatmentProtocolData(r.responseMood, r.responsePain).total);
        return fill.isValid() ? QVariant(fill) : QVariant();
    }
    if (index.column() == kAction) {
        if (role == Qt::DecorationRole && r.delivered) return deliveredTick(r.sham, r.aborted);
        if (role == Qt::TextAlignmentRole) return QVariant(Qt::AlignCenter);
        if (role == Qt::ToolTipRole) {
            if (!r.delivered)
                return r.blockedBy.isEmpty()
                           ? QVariant()
                           : QVariant(QStringLiteral("This sonication would be blocked:\n%1")
                                          .arg(r.blockedBy.join(QLatin1Char('\n'))));
            const QString what =
                r.sham ? QStringLiteral("Sham -- masking audio only, no ultrasound sent")
                       : QStringLiteral("Delivered");
            return r.aborted ? what + QStringLiteral(", stopped before it finished") : what;
        }
    }
    // A row the rules would refuse, marked where the tick goes so the column
    // reads the same way down the page: what has happened, and what cannot.
    if (!r.delivered && !r.blockedBy.isEmpty()) {
        if (role == Qt::ForegroundRole) return QColor(0xff, 0xd9, 0xd6);
        if (role == Qt::BackgroundRole && !current) return QColor(0x4a, 0x1c, 0x1c);
        if (role == Qt::DisplayRole && index.column() == kAction)
            return QStringLiteral("!");
    }
    if (role == Qt::ToolTipRole) {
        // Every line of it, not the latest one the cell has room for.
        if (index.column() == kNotes)
            return r.notes.isEmpty() ? QStringLiteral("Double-click to add a note")
                                     : r.notes;
        // The time columns show the time of day; the date is in the tooltip.
        if (index.column() == kStart) return r.startedAtFull;
        if (index.column() == kEnd) return r.endedAtFull;
        if (index.column() == kStatus) {
            switch (r.statusCode) {
                case 0: return QVariant();
                case 102: return QStringLiteral("102 Processing -- going out now");
                case 200: return QStringLiteral("200 OK -- ran to the end");
                case 499:
                    return QStringLiteral("499 Client Closed Request -- stopped by the operator");
                default: return QString::number(r.statusCode);
            }
        }
        // The columns are elided to keep the whole row on screen, so hovering
        // has to be able to show what was cut -- the parameter string above all,
        // which is the record of what was actually sent.
        const QVariant shown = data(index, Qt::DisplayRole);
        return shown.toString().isEmpty() ? QVariant() : shown;
    }
    if (role == Qt::ForegroundRole) {
        // Anything but 200 is worth seeing without reading the number.
        if (index.column() == kStatus && r.statusCode != 0 && r.statusCode != 200)
            return QColor(0xb1, 0x1c, 0x1c);
        return QVariant();
    }
    if (role == Qt::BackgroundRole) {
        // setTreatmentProtocolTableData.m marks the row the Current Sonication
        // Number points at -- `addStyle(table, sGrey, "row", value)` -- in grey.
        // Dark blue instead: against the light blue of a clicked row the two
        // stay apart, and white text on it reads where grey did not.
        // Only a row still to run: BeamV0 greys the current row over its
        // response, but its number always points past what has been delivered.
        if (current && !r.delivered) return QColor(0xa0, 0xa0, 0xa0);
        // Delivered only: a row must not change colour as a score is typed.
        if (r.delivered) {
            const QColor fill = responseFill(
                beam::gui::getResponseFromTreatmentProtocolData(r.responseMood, r.responsePain)
                    .total);
            if (fill.isValid()) return fill;
        }
        return index.row() % 2 == 0 ? QColor(0xff, 0xff, 0xff) : QColor(0xf2, 0xf4, 0xf7);
    }
    if (role != Qt::DisplayRole && role != Qt::EditRole) {
        return QVariant();
    }
    switch (index.column()) {
        case kAction: return QVariant();  // the view's button lives here
        case kNumber: return r.number;
        case kTarget: return r.target;
        case kDuration: return r.duration;
        case kAmplitude: return r.amplitude;
        case kParameters: return r.parameters;
        case kResponsePain: return r.responsePain;
        case kResponseMood: return r.responseMood;
        case kStart: return r.startedAt;
        case kEnd: return r.endedAt;
        case kStatus: return r.statusCode == 0 ? QString() : QString::number(r.statusCode);
        case kNotes: {
            // One line in a one-line cell: the most recent note, with a count
            // in front of it when there are older ones behind it. The editor
            // and the tooltip have the rest.
            if (role == Qt::EditRole) return r.notes;
            const QStringList lines =
                r.notes.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
            if (lines.isEmpty()) return QString();
            return lines.size() == 1
                       ? lines.last()
                       : QStringLiteral("[%1] %2").arg(lines.size()).arg(lines.last());
        }
        default: return QVariant();
    }
}

bool TreatmentProtocolTableModel::setData(const QModelIndex& index, const QVariant& value, int role) {
    if (role != Qt::EditRole || !index.isValid() || index.row() < 0 ||
        index.row() >= static_cast<int>(rows_.size())) {
        return false;
    }
    TreatmentProtocolRow& r = rows_[static_cast<std::size_t>(index.row())];
    // touched: a response cell was edited, so the whole row repaints. clamped:
    // the value had to be brought into range, which is worth saying out loud.
    // They are not the same thing -- announcing a clamp on every edit buried
    // the message the save itself puts up.
    bool touched = false;
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
            const double typed = value.toDouble();
            r.responsePain = typed;
            const beam::gui::MoodPainResponse resp =
                beam::gui::getResponseFromTreatmentProtocolData(r.responseMood, r.responsePain);
            r.responsePain = resp.pain;  // source writes the clamped value back into the table
            touched = true;
            clamped = resp.pain != typed;
            break;
        }
        case kResponseMood: {
            const double typed = value.toDouble();
            r.responseMood = typed;
            const beam::gui::MoodPainResponse resp =
                beam::gui::getResponseFromTreatmentProtocolData(r.responseMood, r.responsePain);
            r.responseMood = resp.mood;
            touched = true;
            clamped = resp.mood != typed;
            break;
        }
        default:
            return false;
    }

    if (touched) {
        // Both responses feed the row, so the whole row is repainted rather
        // than just the cell that was edited.
        emit dataChanged(this->index(index.row(), 0), this->index(index.row(), kColumnCount - 1),
                         {Qt::DisplayRole, Qt::EditRole, Qt::BackgroundRole});
        if (clamped) emit responseClamped();
    } else {
        emit dataChanged(index, index, {Qt::DisplayRole, Qt::EditRole});
    }
    return true;
}

QVariant TreatmentProtocolTableModel::headerData(int section, Qt::Orientation orientation,
                                                 int role) const {
    if (role == Qt::ToolTipRole && orientation == Qt::Horizontal) {
        // The scale, which the name alone does not give.
        if (section == kResponsePain || section == kResponseMood)
            return QStringLiteral("-2 to 2");
        if (section == kNotes) return QStringLiteral("Double-click a cell to add a note");
        return QVariant();
    }
    if (role != Qt::DisplayRole) {
        return QVariant();
    }
    if (orientation == Qt::Vertical) {
        return section + 1;
    }
    // setTreatmentProtocolTableNames.m's renames.
    switch (section) {
        case kAction: return QString();
        case kNumber: return QStringLiteral("#");
        case kTarget: return QStringLiteral("Target");
        case kDuration: return QStringLiteral("Duration (s)");
        case kAmplitude: return QStringLiteral("Amplitude");
        case kParameters: return QStringLiteral("Parameters");
        case kResponsePain: return QStringLiteral("Response Pain");
        case kResponseMood: return QStringLiteral("Response Mood");
        case kStart: return QStringLiteral("Start");
        case kEnd: return QStringLiteral("End");
        case kStatus: return QStringLiteral("Status");
        case kNotes: return QStringLiteral("Notes");
        default: return QVariant();
    }
}

Qt::ItemFlags TreatmentProtocolTableModel::flags(const QModelIndex& index) const {
    if (!index.isValid()) {
        return Qt::NoItemFlags;
    }
    // The completion time is stamped by the countdown, not typed.
    // Neither is typed into: one is stamped by the countdown, the other holds
    // a button.
    // Notes are edited in their own box, where a note can run to several
    // lines; typing into the cell would replace the record rather than add to
    // it.
    if (index.column() == kStart || index.column() == kEnd || index.column() == kStatus ||
        index.column() == kAction || index.column() == kNotes)
        return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    // A delivered sonication is a record: what was sent cannot be rewritten,
    // only what was observed about it. Locking the cells here rather than
    // dropping the edit later means the edit never appears to take.
    if (index.row() < static_cast<int>(rows_.size()) &&
        rows_[static_cast<std::size_t>(index.row())].delivered &&
        index.column() != kResponsePain && index.column() != kResponseMood &&
        index.column() != kNotes)
        return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable;
}

void TreatmentProtocolTableModel::setCurrentSonication(int oneBased) {
    if (currentSonication_ == oneBased) return;
    currentSonication_ = oneBased;
    if (rows_.empty()) return;
    emit dataChanged(index(0, 0), index(static_cast<int>(rows_.size()) - 1, kColumnCount - 1),
                     {Qt::BackgroundRole, kCurrentRowRole});
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
