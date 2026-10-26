#pragma once

#include "device_link_panel.hpp"
#include "workflow_window.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <exception>
#include <memory>
#include <optional>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <functional>
#include <utility>

#include <QListWidgetItem>
#include <QFile>
#include <QFileDialog>
#include <QHash>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QMouseEvent>
#include <QStyle>
#include <QProxyStyle>
#include <QStyleOptionSlider>
#include <QHelpEvent>
#include <QToolTip>
#include <QSet>
#include <QSignalBlocker>
#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QKeyEvent>
#include <QStringList>
#include <QMessageBox>
#include <QMenu>
#include <QHeaderView>
#include <QIntValidator>
#include <QTableWidgetItem>
#include <QProgressDialog>
#include <QResizeEvent>
#include <QScrollArea>
#include <QTextStream>
#include <QThread>
#include <QTimer>
#include <QStyle>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QProgressBar>
#include <QComboBox>
#include <QTableWidget>
#include <QHeaderView>
#include <QTabWidget>
#include <QListWidget>
#include <QSpacerItem>
#include <QSlider>
#include <QLabel>
#include <QPainter>
#include <QPen>
#include <QStyleOptionSlider>
#include <QEvent>

#include "infra_dicom/load_mri_ras.hpp"
#include "infra_mat/legacy_beam_session.hpp"
#include "array/array_data.hpp"
#include "array/array_struct.hpp"
#include "gui/mri_overlay_presenter.hpp"
#include "mri/fiducial_detect.hpp"
#include "mri/mri_loader.hpp"
#include "mri/ras_transform.hpp"
#include "mri_view.hpp"
#include "registration_fiducial_layout.hpp"
#include "registration/array_transform.hpp"
#include "correction/receive_waveform.hpp"
#include "safety/checks.hpp"
#include "gui_qt/correction_tab_chart.hpp"
#include <QtCharts/QChartView>
#include <QtCharts/QAbstractAxis>
#include <QtCharts/QLegend>
#include "correction/signal.hpp"
#include "serialcom/link_monitor.hpp"
#include "serialcom/serial_port.hpp"

namespace {

// Qt steps a slider by one page when the groove is clicked.
class ClickToPositionStyle final : public QProxyStyle {
public:
    using QProxyStyle::QProxyStyle;

    int styleHint(StyleHint hint, const QStyleOption* option, const QWidget* widget,
                  QStyleHintReturn* returnData) const override {
        if (hint == SH_Slider_AbsoluteSetButtons) return Qt::LeftButton;
        if (hint == SH_Slider_PageSetButtons) return Qt::MiddleButton;
        return QProxyStyle::styleHint(hint, option, widget, returnData);
    }
};

class NumberedSlider final : public QSlider {
public:
    explicit NumberedSlider(Qt::Orientation orientation, QWidget* parent = nullptr)
        : QSlider(orientation, parent) {}

protected:
    void paintEvent(QPaintEvent* event) override {
        QSlider::paintEvent(event);
        QStyleOptionSlider option;
        initStyleOption(&option);
        const QRect groove = style()->subControlRect(QStyle::CC_Slider, &option,
                                                       QStyle::SC_SliderGroove, this);
        const int low = minimum();
        const int high = maximum();
        if (high <= low) return;
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, false);
        const int handleHalf = style()->pixelMetric(QStyle::PM_SliderLength, &option, this) / 2;
        for (int value = low; value <= high; ++value) {
            const bool selected = (value == this->value());
            const QColor ink = !isEnabled() ? QColor("#6a767c")
                               : selected   ? QColor("#ffe08a")
                                            : QColor("#b9cbd1");
            painter.setPen(QPen(ink, 1));
            QFont labelFont(font().family(), 11);
            labelFont.setBold(selected);
            painter.setFont(labelFont);
            const double fraction = static_cast<double>(value - low) / (high - low);
            if (orientation() == Qt::Horizontal) {
                // Match Qt's handle-center endpoints rather than using a fixed inset; the
                const int start = groove.left() + handleHalf;
                const int end = groove.right() - handleHalf;
                const int x = qRound(start + fraction * (end - start));
                const int y = groove.center().y();
                const QString text = QString::number(value);
                if (selected) {
                    painter.setBrush(QColor(91, 74, 28, 210));
                    painter.drawRoundedRect(QRect(x - 10, y + 5, 20, 16), 3, 3);
                }
                painter.drawText(QRect(x - 12, y + 6, 24, 20),
                                 Qt::AlignHCenter | Qt::AlignTop, text);
            } else {
                const int start = groove.top() + handleHalf;
                const int end = groove.bottom() - handleHalf;
                const int y = qRound(end - fraction * (end - start));
                const int x = groove.center().x();
                if (selected) {
                    painter.setBrush(QColor(91, 74, 28, 210));
                    painter.drawRoundedRect(QRect(x + 6, y - 10, 20, 20), 3, 3);
                }
                painter.drawText(QRect(x + 7, y - 9, width() - x - 7, 18),
                                 Qt::AlignLeft | Qt::AlignVCenter, QString::number(value));
            }
        }
    }
};

class CalibrationPaneMouseFilter final : public QObject {
public:
    CalibrationPaneMouseFilter(QObject* parent, std::function<void()> onPress)
        : QObject(parent), onPress_(std::move(onPress)) {}

protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type() == QEvent::MouseButtonPress && onPress_)
            onPress_();
        return QObject::eventFilter(watched, event);
    }

private:
    std::function<void()> onPress_;
};

class TargetingExampleCanvas final : public QWidget {
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.fillRect(rect(), QColor("#102b35"));
        painter.setRenderHint(QPainter::Antialiasing);
        const QPointF center = rect().center();
        painter.setPen(QPen(QColor("#5ccfe8"), 1));
        painter.drawLine(QPointF(20, center.y()), QPointF(width() - 20, center.y()));
        painter.drawLine(QPointF(center.x(), 20), QPointF(center.x(), height() - 20));
        painter.setPen(QPen(QColor("#ff6b8a"), 2));
        painter.setBrush(QColor(255, 107, 138, 45));
        painter.drawEllipse(center, 42, 42);
        painter.setBrush(QColor("#ffd166"));
        for (const QPointF& p : {center + QPointF(-58, -24), center + QPointF(-58, 24),
                                 center + QPointF(58, 0)})
            painter.drawEllipse(p, 6, 6);
        painter.setPen(QColor("#dcebef"));
        painter.drawText(QRect(20, 8, width() - 40, 22), Qt::AlignCenter,
                         QStringLiteral("Targeting example — target and three-point layout"));
    }
};

}  // namespace
#include "ui_workflow_shell.h"

namespace beam::app {
namespace {

constexpr std::size_t stageCount = static_cast<std::size_t>(beam::gui::WorkflowStage::Count);

Eigen::MatrixXd readArrayRectCsv(const QString& path) {
    std::ifstream file(path.toStdString());
    if (!file) throw std::runtime_error("Cannot open default transducer geometry: " + path.toStdString());
    std::vector<std::vector<double>> rows;
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        std::stringstream stream(line);
        std::string cell;
        std::vector<double> row;
        while (std::getline(stream, cell, ',')) row.push_back(std::stod(cell));
        rows.push_back(std::move(row));
    }
    if (rows.size() != 19 || rows.front().empty()) throw std::runtime_error("Invalid transducer geometry CSV");
    const Eigen::Index columns = static_cast<Eigen::Index>(rows.front().size());
    Eigen::MatrixXd result(19, columns);
    for (Eigen::Index r = 0; r < 19; ++r) {
        if (static_cast<Eigen::Index>(rows[static_cast<std::size_t>(r)].size()) != columns)
            throw std::runtime_error("Ragged transducer geometry CSV");
        for (Eigen::Index c = 0; c < columns; ++c)
            result(r, c) = rows[static_cast<std::size_t>(r)][static_cast<std::size_t>(c)];
    }
    return result;
}


// An axis holds voxel CENTRES, but drawImage stretches the slice's n voxels edge to edge across
std::pair<double, double> axisEdgesMm(const Eigen::VectorXd& axis) {
    if (axis.size() < 2) return {0.0, 1.0};
    const double first = axis(0);
    const double last = axis(axis.size() - 1);
    const double step = (last - first) / static_cast<double>(axis.size() - 1);
    return {first - step / 2.0, last + step / 2.0};
}

double normalizedAxisPosition(const Eigen::VectorXd& axis, double mm) {
    const auto [low, high] = axisEdgesMm(axis);
    if (axis.size() < 2 || high == low) return 0.5;
    return std::clamp((mm - low) / (high - low), 0.0, 1.0);
}

// Bounds for the shared MRI viewer height chosen in syncMriViewerHeights().
constexpr double kTargetViewZoom = 4.0;

constexpr int kMinMriViewerHeight = 300;

// A default slider is a hairline with a small hollow box on it, which on these grounds reads as
const char* const kSliderStyle =
    "QSlider::groove:horizontal { height: 6px; border-radius: 3px; background: #55727f; } "
    "QSlider::add-page:horizontal { background: #55727f; border-radius: 3px; } "
    "QSlider::sub-page:horizontal { background: #45c8ea; border-radius: 3px; } "
    "QSlider::handle:horizontal { width: 12px; height: 12px; margin: -5px 0; "
    "background: #ffffff; border: 2px solid #1b3b47; border-radius: 8px; } "
    "QSlider::handle:horizontal:hover { border-color: #45c8ea; } "
    "QSlider::groove:vertical { width: 6px; border-radius: 3px; background: #55727f; } "
    "QSlider::sub-page:vertical { background: #55727f; border-radius: 3px; } "
    "QSlider::add-page:vertical { background: #45c8ea; border-radius: 3px; } "
    "QSlider::handle:vertical { width: 12px; height: 12px; margin: 0 -5px; "
    "background: #ffffff; border: 2px solid #1b3b47; border-radius: 8px; } "
    "QSlider::handle:vertical:hover { border-color: #45c8ea; } "
    "QSlider::groove:disabled { background: #44565e; } "
    "QSlider::sub-page:horizontal:disabled { background: #7e8f96; } "
    "QSlider::add-page:vertical:disabled { background: #7e8f96; } "
    "QSlider::handle:disabled { background: #d7dee1; border-color: #7e8f96; }";
constexpr int kMaxMriViewerHeight = 900;

// The two Registration columns are the same height above their next step: the fiducial diagram
constexpr int kRegistrationColumnHeight = 186;

// Calibrated on one subject: the five accurate markers score 0.78-0.85
constexpr double kFiducialTrustThreshold = 0.70;
// Below this, reported as probably wrong rather than merely unverified.
constexpr double kFiducialRejectThreshold = 0.50;

// Safe at this size because the detector drops candidates belonging to a neighbouring marker.
constexpr double kSingleFiducialSearchRadiusMm = 12.0;

enum class FiducialTrust { Unscored, Reject, Caution, Trusted };

FiducialTrust fiducialTrustOf(double confidence) {
    if (confidence <= 0.0) return FiducialTrust::Unscored;
    if (confidence < kFiducialRejectThreshold) return FiducialTrust::Reject;
    if (confidence < kFiducialTrustThreshold) return FiducialTrust::Caution;
    return FiducialTrust::Trusted;
}

QString statusSymbol(beam::gui::WorkflowStatus status) {
    switch (status) {
        case beam::gui::WorkflowStatus::Complete: return QStringLiteral("✓");
        case beam::gui::WorkflowStatus::Blocked: return QStringLiteral("!");
        case beam::gui::WorkflowStatus::Invalidated: return QStringLiteral("↻");
        case beam::gui::WorkflowStatus::InProgress: return QStringLiteral("●");
        case beam::gui::WorkflowStatus::NotStarted: return QStringLiteral("○");
    }
    return QStringLiteral("?");
}

}  // namespace
}  // namespace beam::app
