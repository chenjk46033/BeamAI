#include "workflow_window.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <memory>
#include <optional>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <functional>
#include <utility>

#include <QListWidgetItem>
#include <QFileDialog>
#include <QFile>
#include <QTextStream>
#include <QSet>
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
#include <QSplitter>
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
#include "mri/mri_loader.hpp"
#include "mri/ras_transform.hpp"
#include "mri_view.hpp"
#include "registration/array_transform.hpp"
#include "correction/receive_waveform.hpp"
#include "correction/signal.hpp"
#include "serialcom/serial_port.hpp"

namespace {

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
            painter.setPen(QPen(selected ? QColor("#ffe08a") : QColor("#b9cbd1"), 1));
            QFont labelFont(font().family(), 11);
            labelFont.setBold(selected);
            painter.setFont(labelFont);
            const double fraction = static_cast<double>(value - low) / (high - low);
            if (orientation() == Qt::Horizontal) {
                // Match Qt's handle-center endpoints rather than using a
                // fixed inset; the labels are centered on the actual track.
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

// defaultSubjectArrayRect.csv contains both physical panels in one matrix.
// BeamV0's saved MAT structure has separate left/right ArrayStruct entries,
// but defineArrayData(rect) alone duplicates the combined matrix into both.
// Reconstruct those two entries from the panel separation before computing
// fiducials; otherwise both panel centres collapse to the midline.
void reconstructPhysicalArrayHalves(beam::array::ArrayData& data) {
    const Eigen::MatrixXd& total = data.arrayTotal.rect;
    const double midline = total.row(16).mean();
    std::array<std::vector<Eigen::Index>, 2> columns;
    for (Eigen::Index c = 0; c < total.cols(); ++c) {
        // MATLAB designation 1 is Right, designation 2 is Left.
        columns[total(16, c) >= midline ? 0 : 1].push_back(c);
    }
    if (columns[0].empty() || columns[1].empty())
        throw std::runtime_error("Default transducer geometry does not contain distinct left/right panels");
    for (std::size_t half = 0; half < 2; ++half) {
        Eigen::MatrixXd rect(total.rows(), static_cast<Eigen::Index>(columns[half].size()));
        for (Eigen::Index c = 0; c < rect.cols(); ++c)
            rect.col(c) = total.col(columns[half][static_cast<std::size_t>(c)]);
        data.array[half] = beam::array::defineArrayStruct(rect, data.arrayTotal.frequency,
                                                          data.arrayTotal.elementDimensions);
        data.array[half].elementMapping = static_cast<int>(half) + 1;
    }
}

double normalizedAxisPosition(const Eigen::VectorXd& axis, double mm) {
    if (axis.size() < 2 || axis(axis.size() - 1) == axis(0)) return 0.5;
    return std::clamp((mm - axis(0)) / (axis(axis.size() - 1) - axis(0)), 0.0, 1.0);
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

WorkflowWindow::WorkflowWindow(QWidget* parent) : QMainWindow(parent), ui_(new Ui::WorkflowShell) {
    ui_->setupUi(this);
    // Align each workflow body with the top of the process-navigation panel;
    // the large page-title band is redundant once the stage list identifies
    // the active step.
    ui_->pageTitle->hide();
    ui_->pageSubtitle->hide();
    ui_->contentLayout->setContentsMargins(28, 0, 28, 12);
    ui_->registrationInstructions->setStyleSheet(
        QStringLiteral("QLabel { color: #17313f; background: #f1f8fa; "
                       "border: 1px solid #b7d1d9; border-radius: 4px; "
                       "padding: 8px; }"));
    ui_->registrationInstructions->setText(QStringLiteral(
        "Select a fiducial row to navigate and zoom. Drag the selected red ring onto the grayscale donut, "
        "verify it in all three MRI views, and repeat for all six. To enter a point directly, right-click the "
        "MRI point, choose marker 1-6 from the menu, and paste it into that marker-table row."));
    ui_->registrationInstructions->hide();
    // Use the original deep slate-blue work surface.  Inputs/tables retain
    // their own light surfaces for readability, while the workflow canvas is
    // deliberately dark to reduce glare during MRI review.
    // Match the established neutral dark-gray palette used by the startup
    // screen; this intentionally avoids a blue cast on the main work area.
    const QColor bodySurface(27, 27, 27);
    for (QWidget* page : {ui_->casePage, ui_->systemPage, ui_->imagingPage,
                          ui_->registrationPage, ui_->placeholderPage}) {
        page->setAutoFillBackground(true);
        QPalette palette = page->palette();
        palette.setColor(QPalette::Window, bodySurface);
        palette.setColor(QPalette::Base, Qt::white);
        palette.setColor(QPalette::WindowText, QColor(242, 247, 248));
        palette.setColor(QPalette::Text, QColor(242, 247, 248));
        page->setPalette(palette);
    }
    // The page palette is intentionally light-text for the dark work surface.
    // Restore an explicit dark foreground for editable fields so entered case
    // identifiers remain visible on their light input backgrounds.
    for (QLineEdit* edit : findChildren<QLineEdit*>()) {
        edit->setStyleSheet(QStringLiteral(
            "QLineEdit { background: #ffffff; color: #17313f; selection-background-color: #58c7e5; "
            "selection-color: #083d4d; } QLineEdit:disabled { color: #65757d; background: #edf1f2; }"));
        QPalette palette = edit->palette();
        palette.setColor(QPalette::Base, QColor(255, 255, 255));
        palette.setColor(QPalette::Text, QColor(23, 49, 63));
        palette.setColor(QPalette::PlaceholderText, QColor(105, 121, 129));
        edit->setPalette(palette);
    }
    for (QComboBox* combo : findChildren<QComboBox*>()) {
        combo->setStyleSheet(QStringLiteral(
            "QComboBox { background: #ffffff; color: #17313f; } "
            "QComboBox QAbstractItemView { background: #ffffff; color: #17313f; selection-background-color: #58c7e5; }"));
    }
    ui_->stageList->setAutoFillBackground(true);
    QPalette stagePalette = ui_->stageList->palette();
    stagePalette.setColor(QPalette::Base, QColor(36, 36, 36));
    stagePalette.setColor(QPalette::Window, QColor(36, 36, 36));
    stagePalette.setColor(QPalette::Text, QColor(242, 247, 248));
    stagePalette.setColor(QPalette::WindowText, QColor(242, 247, 248));
    ui_->stageList->setPalette(stagePalette);
    ui_->registrationOverlayControls->setSpacing(4);
    for (QCheckBox* checkBox : {ui_->registrationShowFieldCheckBox,
                                ui_->registrationShowTargetCheckBox,
                                ui_->registrationShowTransducersCheckBox,
                                ui_->registrationShowFiducialsCheckBox,
                                ui_->registrationShowLinkedNavigationCheckBox}) {
        checkBox->setStyleSheet(QStringLiteral("QCheckBox { color: #f2f7f8; spacing: 5px; }"));
        QPalette palette = checkBox->palette();
        palette.setColor(QPalette::WindowText, QColor(242, 247, 248));
        palette.setColor(QPalette::Text, QColor(242, 247, 248));
        checkBox->setPalette(palette);
        checkBox->setMinimumWidth(checkBox->sizeHint().width());
    }
    for (QLabel* label : {ui_->registrationBrightnessControlsLabel,
                          ui_->registrationTransparencyLabel})
        label->setStyleSheet(QStringLiteral("QLabel { color: #f2f7f8; }"));
    for (QToolButton* button : {ui_->registrationBrightnessUpButton,
                                ui_->registrationBrightnessDownButton,
                                ui_->registrationResetAllMriViewsButton})
        button->setStyleSheet(QStringLiteral("QToolButton { color: #f2f7f8; }"));
    // The generic case subtitle consumes vertical space on every workflow
    // page; Registration needs that space for its review and acceptance
    // controls.
    ui_->pageSubtitle->hide();

    // Registration is kept as a fixed, non-scrolling page. Its compact MRI,
    // table, and action layouts are sized to keep acceptance controls visible.

    // Registration contains additional controls below the MRI row, so its
    // layout can otherwise compress the image widgets vertically.  Once the
    // window has been laid out, use the Imaging preview height as the shared
    // reference for both tabs.
    QTimer::singleShot(0, this, [this] { syncRegistrationPreviewHeights(); });

    // Keep the registration actions in their Designer order: immediately
    // below the marker table, after the operator has reviewed the layouts.
    // Make the marker diagram/table split user-adjustable.  The diagram is
    // deliberately on the left, while the table remains on the right.
    ui_->registrationDetailsLayout->removeWidget(ui_->registrationFiducialLayout);
    ui_->registrationDetailsLayout->removeWidget(ui_->registrationTable);
    auto* markerSplitter = new QSplitter(Qt::Horizontal, this);
    markerSplitter->setChildrenCollapsible(false);
    markerSplitter->setHandleWidth(12);
    markerSplitter->setMinimumHeight(196);
    markerSplitter->setCursor(Qt::SplitHCursor);
    markerSplitter->setToolTip(QStringLiteral("Drag the divider left or right to resize the fiducial diagram and marker table"));
    markerSplitter->setStyleSheet(QStringLiteral(
        "QSplitter::handle:horizontal { background: #8aa1aa; border-left: 1px solid #5e7882; "
        "border-right: 1px solid #5e7882; margin: 0; }"
        "QSplitter::handle:horizontal:hover { background: #2b91ad; }"));
    ui_->registrationActions->removeWidget(ui_->placeFiducialButton);
    ui_->placeFiducialButton->hide();
    ui_->registrationActions->removeWidget(ui_->resetRegistrationButton);
    ui_->resetRegistrationButton->setText(QStringLiteral("Restore fiducials"));
    ui_->resetRegistrationButton->setMinimumHeight(30);
    auto* markerDiagramPanel = new QWidget(markerSplitter);
    markerDiagramPanel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto* markerDiagramLayout = new QVBoxLayout(markerDiagramPanel);
    markerDiagramLayout->setContentsMargins(0, 0, 0, 0);
    markerDiagramLayout->setSpacing(0);
    auto* triangleColumn = new QVBoxLayout;
    triangleColumn->setContentsMargins(0, 0, 4, 0);
    triangleColumn->setSpacing(0);
    registrationStep1Label_ = new QLabel(QStringLiteral("1"), markerDiagramPanel);
    registrationStep1Label_->setStyleSheet(QStringLiteral(
        "QLabel { color: #dff7ff; background: rgba(15, 54, 68, 190); border-radius: 3px; font-size: 30px; font-weight: 700; }"));
    registrationStep1Label_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    registrationStep1Label_->setFixedSize(28, 34);
    registrationStep1Label_->setAttribute(Qt::WA_TransparentForMouseEvents);
    registrationStep1Label_->setParent(markerDiagramPanel);
    registrationStep1Label_->move(4, 4);
    registrationStep1Label_->show();
    registrationStep1Label_->raise();
    triangleColumn->addWidget(ui_->registrationFiducialLayout, 0, Qt::AlignTop);
    triangleColumn->addWidget(ui_->resetRegistrationButton);
    markerDiagramLayout->addLayout(triangleColumn);
    markerSplitter->addWidget(markerDiagramPanel);
    ui_->registrationFiducialLayout->setMinimumWidth(300);
    ui_->registrationFiducialLayout->setMinimumHeight(0);
    ui_->registrationFiducialLayout->setMaximumHeight(120);
    ui_->registrationFiducialLayout->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    ui_->registrationFiducialLayout->setFixedHeight(120);
    markerDiagramPanel->setMinimumHeight(264);
    markerDiagramPanel->setMaximumHeight(QWIDGETSIZE_MAX);
    for (QVBoxLayout* layout : {ui_->registrationSagittalLayout,
                                ui_->registrationCoronalLayout,
                                ui_->registrationAxialLayout}) {
        layout->setContentsMargins(4, 3, 4, 2);
        layout->setSpacing(0);
    }
    for (QLabel* label : {ui_->registrationSagittalLabel,
                          ui_->registrationCoronalLabel,
                          ui_->registrationAxialLabel}) {
        label->setFixedHeight(14);
        label->setMinimumHeight(14);
        label->setMaximumHeight(14);
        label->setFixedWidth(78);
        label->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        label->setContentsMargins(0, 0, 0, 0);
        label->setMargin(0);
        label->setIndent(0);
        label->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
        label->setStyleSheet(QStringLiteral("QLabel { font-size: 10px; padding: 0; }"));
    }
    for (QVBoxLayout* layout : {ui_->sagittalLayout, ui_->coronalLayout, ui_->axialLayout}) {
        layout->setContentsMargins(4, 3, 4, 2);
        layout->setSpacing(0);
    }
    for (QLabel* label : {ui_->sagittalSliceLabel, ui_->coronalSliceLabel, ui_->axialSliceLabel}) {
        label->setFixedHeight(14);
        label->setMinimumHeight(14);
        label->setMaximumHeight(14);
        label->setFixedWidth(78);
        label->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        label->setContentsMargins(0, 0, 0, 0);
        label->setMargin(0);
        label->setIndent(0);
        label->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
        label->setStyleSheet(QStringLiteral("QLabel { font-size: 10px; padding: 0; }"));
    }
    ui_->sagittalLayout->setAlignment(ui_->sagittalSliceLabel, Qt::AlignHCenter | Qt::AlignTop);
    ui_->coronalLayout->setAlignment(ui_->coronalSliceLabel, Qt::AlignHCenter | Qt::AlignTop);
    ui_->axialLayout->setAlignment(ui_->axialSliceLabel, Qt::AlignHCenter | Qt::AlignTop);
    ui_->registrationSagittalLayout->setAlignment(ui_->registrationSagittalLabel, Qt::AlignHCenter | Qt::AlignTop);
    ui_->registrationCoronalLayout->setAlignment(ui_->registrationCoronalLabel, Qt::AlignHCenter | Qt::AlignTop);
    ui_->registrationAxialLayout->setAlignment(ui_->registrationAxialLabel, Qt::AlignHCenter | Qt::AlignTop);
    auto* markerTablePanel = new QWidget(markerSplitter);
    markerTablePanel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto* markerTableLayout = new QVBoxLayout(markerTablePanel);
    markerTableLayout->setContentsMargins(0, 0, 0, 0);
    markerTableLayout->setSpacing(0);
    ui_->registrationActions->removeWidget(ui_->registerFiducialsButton);
    registrationStep2Label_ = new QLabel(QStringLiteral("2"), markerTablePanel);
    registrationStep2Label_->setStyleSheet(QStringLiteral(
        "QLabel { color: #dff7ff; background: rgba(15, 54, 68, 190); border-radius: 3px; font-size: 30px; font-weight: 700; }"));
    registrationStep2Label_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    registrationStep2Label_->setFixedSize(20, 20);
    registrationStep2Label_->setAttribute(Qt::WA_TransparentForMouseEvents);
    registrationStep2Label_->setParent(ui_->registrationTable->horizontalHeader());
    registrationStep2Label_->move(0, 0);
    registrationStep2Label_->show();
    registrationStep2Label_->raise();
    ui_->confirmFiducialButton->setText(QStringLiteral("Confirm all located fiducials"));
    ui_->confirmFiducialButton->setToolTip(QStringLiteral(
        "Confirm every fiducial currently marked Located after reviewing the MRI views"));
    ui_->confirmFiducialButton->setMinimumHeight(30);
    ui_->confirmFiducialButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    ui_->confirmFiducialButton->setStyleSheet(QStringLiteral(
        "QPushButton { background: #176b87; color: white; border: 1px solid #0f5269; border-radius: 4px; padding: 5px 8px; font-weight: 600; } "
        "QPushButton:hover { background: #2083a3; } QPushButton:disabled { background: #414141; color: #8b8b8b; border-color: #555555; }"));
    ui_->registerFiducialsButton->setMinimumHeight(30);
    ui_->registerFiducialsButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    ui_->registerFiducialsButton->setStyleSheet(QStringLiteral(
        "QPushButton { background: #176b87; color: white; border: 1px solid #0f5269; border-radius: 4px; padding: 5px 8px; font-weight: 600; } "
        "QPushButton:hover { background: #2083a3; } QPushButton:disabled { background: #414141; color: #8b8b8b; border-color: #555555; }"));
    auto* markerActions = new QHBoxLayout;
    markerActions->setContentsMargins(0, 0, 0, 0);
    markerActions->setSpacing(4);
    markerActions->addWidget(ui_->confirmFiducialButton, 1);
    markerActions->addWidget(ui_->registerFiducialsButton, 1);
    markerTableLayout->addWidget(ui_->registrationTable, 0);
    markerTableLayout->addLayout(markerActions);
    markerTableLayout->setAlignment(ui_->registrationTable, Qt::AlignTop);
    markerTableLayout->setAlignment(markerActions, Qt::AlignTop);
    ui_->registrationLayout->removeWidget(ui_->acceptRegistrationButton);
    ui_->acceptRegistrationButton->setMinimumHeight(30);
    registrationStep4Label_ = new QLabel(QStringLiteral("4"), markerTablePanel);
    registrationStep4Label_->setStyleSheet(QStringLiteral(
        "QLabel { color: #dff7ff; background: rgba(15, 54, 68, 190); border-radius: 3px; font-size: 30px; font-weight: 700; }"));
    registrationStep4Label_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    registrationStep4Label_->setFixedSize(28, 34);
    registrationStep4Label_->setAttribute(Qt::WA_TransparentForMouseEvents);
    registrationStep4Label_->setParent(ui_->acceptRegistrationButton);
    registrationStep4Label_->show();
    registrationStep4Label_->raise();
    QTimer::singleShot(0, this, [this, markerTablePanel] {
        if (registrationStep2Label_ && ui_->registrationTable->parentWidget() == markerTablePanel) {
            registrationStep2Label_->setGeometry(ui_->registrationTable->geometry().adjusted(2, 2, -2, -2));
            registrationStep2Label_->raise();
        }
        if (registrationStep4Label_ && ui_->acceptRegistrationButton->parentWidget() == markerTablePanel) {
            registrationStep4Label_->setGeometry(ui_->acceptRegistrationButton->geometry().adjusted(2, 0, -2, 0));
            registrationStep4Label_->raise();
        }
    });
    markerTableLayout->addWidget(ui_->acceptRegistrationButton);
    markerSplitter->addWidget(markerTablePanel);
    markerSplitter->setStretchFactor(0, 1);
    markerSplitter->setStretchFactor(1, 1);
    markerSplitter->setSizes({600, 600});
    ui_->registrationDetailsLayout->addWidget(markerSplitter);
    // The Designer action row is now empty because its buttons were moved into
    // the two splitter panes. Remove that empty layout so it cannot leave a
    // blank band before the final Accept Registration button.
    ui_->registrationLayout->removeItem(ui_->registrationActions);
    ui_->registrationLayout->removeWidget(ui_->registrationResult);
    ui_->registrationLayout->setSpacing(0);
    ui_->registrationResult->setMaximumHeight(28);
    ui_->registrationResult->hide();
    ui_->registrationDetailsLayout->removeWidget(ui_->registrationResult);
    ui_->registrationDetailsLayout->setContentsMargins(0, 0, 0, 0);
    ui_->registrationDetailsLayout->setSpacing(0);
    ui_->registrationActions->removeWidget(ui_->confirmFiducialButton);

    // BeamV0's second registration stage: calibrate the array's lock position
    // after the MRI-fiducial fit. Left and right lock sliders remain separate
    // so an alignment mismatch can be reported before applying the offset.
    calibrationGroup_ = new QGroupBox(QStringLiteral("Transducer lock position"), this);
    auto* calibrationGroup = calibrationGroup_;
    auto* calibrationLayout = new QVBoxLayout(calibrationGroup);
    calibrationLayout->setContentsMargins(8, 4, 8, 4);
    calibrationLayout->setSpacing(8);
    auto* calibrationControls = new QHBoxLayout;
    calibrationControls->setSpacing(8);
    calibrationControls->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    calibrationLayout->addLayout(calibrationControls);
    registrationStep3Label_ = new QLabel(QStringLiteral("3"), calibrationGroup);
    registrationStep3Label_->setStyleSheet(QStringLiteral(
        "QLabel { color: #dff7ff; background: rgba(15, 54, 68, 190); border-radius: 3px; font-size: 30px; font-weight: 700; }"));
    registrationStep3Label_->setAlignment(Qt::AlignCenter);
    registrationStep3Label_->setFixedWidth(28);
    registrationStep3Label_->setFixedSize(28, 34);
    registrationStep3Label_->setAttribute(Qt::WA_TransparentForMouseEvents);
    registrationStep3Label_->setParent(calibrationGroup);
    // Keep the step indicator in the content area.  Placing it at the
    // group's top edge overlaps the QGroupBox title (for example, turning
    // "Subject Left" into "bject Left"), especially when the pane is narrow.
    // The calibration panels begin below the group title, so keep the
    // indicator well inside the content area rather than over either panel's
    // title.  The previous position still intersected the child group-box
    // title after layout margins were applied.
    registrationStep3Label_->move(4, 112);
    registrationStep3Label_->show();
    registrationStep3Label_->raise();
    auto makePositionSlider = [](QWidget* parent, Qt::Orientation orientation) {
        auto* slider = new NumberedSlider(orientation, parent);
        slider->setRange(1, 4);
        slider->setValue(1);
        slider->setSingleStep(1);
        slider->setPageStep(1);
        slider->setTickPosition(QSlider::NoTicks);
        slider->setToolTip(QStringLiteral("Array lock position; each step is 7.5 mm horizontal or 10 mm vertical"));
        slider->setStyleSheet(QStringLiteral(
            "QSlider::groove:horizontal { height: 7px; background: #24495a; border: 1px solid #39758e; border-radius: 3px; } "
            "QSlider::handle:horizontal { width: 16px; margin: -5px 0; background: #58d9f2; border: 1px solid #b9f3ff; border-radius: 8px; } "
            "QSlider::sub-page:horizontal { background: #1686a8; border-radius: 3px; } "
            "QSlider::groove:vertical { width: 7px; background: #24495a; border: 1px solid #39758e; border-radius: 3px; } "
            "QSlider::handle:vertical { height: 16px; margin: 0 -5px; background: #58d9f2; border: 1px solid #b9f3ff; border-radius: 8px; } "
            "QSlider::add-page:vertical { background: #1686a8; border-radius: 3px; }"));
        return slider;
    };
    auto makeLockPanel = [&](const QString& title, QSlider*& horizontal, QSlider*& vertical) {
        auto* panel = new QGroupBox(title, calibrationGroup);
        panel->setMinimumWidth(195);
        panel->setMinimumHeight(145);
        panel->setMaximumHeight(145);
        panel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        auto* panelLayout = new QGridLayout(panel);
        panelLayout->setContentsMargins(5, 2, 5, 2);
        panelLayout->setHorizontalSpacing(7);
        panelLayout->setVerticalSpacing(2);
        panelLayout->setColumnMinimumWidth(1, 22);
        vertical = makePositionSlider(panel, Qt::Vertical);
        horizontal = makePositionSlider(panel, Qt::Horizontal);
        vertical->setFixedHeight(88);
        vertical->setFixedWidth(50);
        vertical->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        horizontal->setMinimumWidth(120);
        horizontal->setMinimumHeight(48);
        horizontal->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        panelLayout->addWidget(new QLabel(QStringLiteral("Vertical"), panel), 0, 0, 1, 2, Qt::AlignCenter);
        panelLayout->addWidget(vertical, 1, 0, 2, 2, Qt::AlignCenter);
        panelLayout->addWidget(new QLabel(QStringLiteral("Horizontal"), panel), 0, 2, Qt::AlignCenter);
        panelLayout->addWidget(horizontal, 1, 2, 2, 1, Qt::AlignCenter);
        return panel;
    };
    calibrationControls->addWidget(makeLockPanel(QStringLiteral("Subject Left"),
                                                  leftHorizontalPositionSlider_, leftVerticalPositionSlider_), 0);
    calibrationControls->addWidget(makeLockPanel(QStringLiteral("Subject Right"),
                                                  rightHorizontalPositionSlider_, rightVerticalPositionSlider_), 0);
    registerCurrentPositionButton_ = new QPushButton(QStringLiteral("Register Arrays to Current Position"), calibrationGroup);
    registerCurrentPositionButton_->setMinimumHeight(30);
    registerCurrentPositionButton_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    registerCurrentPositionButton_->setStyleSheet(QStringLiteral(
        "QPushButton { background: #176b87; color: white; border: 1px solid #0f5269; border-radius: 4px; padding: 5px 8px; font-weight: 600; } "
        "QPushButton:hover { background: #2083a3; } "
        "QPushButton:disabled { background: #414141; color: #8b8b8b; border-color: #555555; }"));
    registerCurrentPositionButton_->setToolTip(QStringLiteral(
        "Apply the BeamV0 lock-position offset after registering the array to MRI fiducials"));
    calibrationLayout->addWidget(registerCurrentPositionButton_);
    calibrationGroup->setMinimumHeight(0);
    calibrationGroup->setMaximumHeight(QWIDGETSIZE_MAX);
    calibrationGroup->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    markerDiagramLayout->addWidget(calibrationGroup, 1);
    // Calibration sliders use their own sliderPressed/valueChanged handlers
    // below. Do not install a parent mouse filter here: re-entering
    // updateRegistrationAvailability() from a slider mouse event can change
    // the enabled state while Qt is dispatching that same drag, leaving the
    // calibration pane apparently locked.
    // Child views/tables are added after the step labels are created; raise
    // the overlays once the pane hierarchy is complete so they remain visible
    // even before any MRI or fiducial data exists.
    registrationStep1Label_->raise();
    registrationStep2Label_->raise();
    registrationStep3Label_->raise();
    registrationStep4Label_->raise();
    // Registration feedback belongs inside the Registration page, below its
    // acceptance action, rather than in the global footer line.
    ui_->registrationResult->show();
    markerTableLayout->addWidget(ui_->registrationResult);
    auto invalidateCurrentPositionRegistration = [this] {
        if (registrationLockPositionRegistered())
            setRegistrationPhase(RegistrationPhase::FitApplied);
        else
            updateRegistrationAvailability();
    };
    for (QSlider* slider : {leftHorizontalPositionSlider_, leftVerticalPositionSlider_,
                            rightHorizontalPositionSlider_, rightVerticalPositionSlider_})
        connect(slider, &QSlider::valueChanged, this, [invalidateCurrentPositionRegistration] {
            invalidateCurrentPositionRegistration();
        });
    for (QSlider* slider : {leftHorizontalPositionSlider_, leftVerticalPositionSlider_,
                            rightHorizontalPositionSlider_, rightVerticalPositionSlider_})
        connect(slider, &QSlider::sliderPressed, this, [this] {
            // Pressing a calibration track is an explicit request to review
            // or redo Step 3, even when the handle is pressed at its current
            // value and therefore emits no valueChanged signal.
            if (registrationLockPositionRegistered())
                setRegistrationPhase(RegistrationPhase::FitApplied);
            else
                updateRegistrationAvailability();
        });
    connect(registerCurrentPositionButton_, &QPushButton::clicked, this,
            [this] { performCurrentPositionRegistration(); });
    registerCurrentPositionButton_->setEnabled(false);

    // The imaging page starts as an uncluttered source-data review; users can
    // explicitly reveal fiducials there. Registration keeps them visible.
    ui_->showFiducialsCheckBox->setChecked(false);
    for (QToolButton* button : {ui_->sagittalPreviousButton, ui_->coronalPreviousButton,
                                ui_->axialPreviousButton, ui_->registrationSagittalPreviousButton,
                                ui_->registrationCoronalPreviousButton, ui_->registrationAxialPreviousButton}) {
        button->setIcon(QIcon());
        button->setText(QString(QChar(0x25C0)));
    }
    for (QToolButton* button : {ui_->sagittalNextButton, ui_->coronalNextButton,
                                ui_->axialNextButton, ui_->registrationSagittalNextButton,
                                ui_->registrationCoronalNextButton, ui_->registrationAxialNextButton}) {
        button->setIcon(QIcon());
        button->setText(QString(QChar(0x25B6)));
    }
    for (QToolButton* button : {ui_->resetSagittalButton, ui_->resetCoronalButton,
                                ui_->resetAxialButton, ui_->registrationResetSagittalButton,
                                ui_->registrationResetCoronalButton, ui_->registrationResetAxialButton,
                                ui_->resetAllMriViewsButton, ui_->registrationResetAllMriViewsButton}) {
        button->setIcon(QIcon());
        button->setText(QString(QChar(0x21BA)));
    }
    for (QToolButton* button : {ui_->brightnessDownButton, ui_->registrationBrightnessDownButton}) {
        button->setIcon(QIcon());
        button->setText(QString(QChar(0x2193)));
    }
    for (QToolButton* button : {ui_->brightnessUpButton, ui_->registrationBrightnessUpButton}) {
        button->setIcon(QIcon());
        button->setText(QString(QChar(0x2191)));
    }
    const QString sliceButtonStyle = QStringLiteral(
        "QToolButton { background: #f7fafb; color: #17313f; border: 1px solid #9eacb4; "
        "border-radius: 3px; min-width: 22px; max-width: 22px; min-height: 20px; max-height: 20px; padding: 0; "
        "font-family: 'Segoe UI Symbol'; font-size: 12px; font-weight: 700; } "
        "QToolButton:hover { background: #e3f2f6; border-color: #2888a2; } "
        "QToolButton:pressed { background: #cce7ee; } "
        "QToolButton:disabled { background: #e8ecee; border-color: #c9d0d4; }");
    for (QToolButton* button : {ui_->sagittalPreviousButton, ui_->sagittalNextButton,
                                ui_->coronalPreviousButton, ui_->coronalNextButton,
                                ui_->axialPreviousButton, ui_->axialNextButton,
                                ui_->resetSagittalButton, ui_->resetCoronalButton,
                                ui_->resetAxialButton, ui_->registrationSagittalPreviousButton,
                                ui_->registrationSagittalNextButton, ui_->registrationCoronalPreviousButton,
                                ui_->registrationCoronalNextButton, ui_->registrationAxialPreviousButton,
                                ui_->registrationAxialNextButton, ui_->registrationResetSagittalButton,
                                ui_->registrationResetCoronalButton, ui_->registrationResetAxialButton,
                                ui_->brightnessDownButton, ui_->brightnessUpButton,
                                ui_->registrationBrightnessDownButton, ui_->registrationBrightnessUpButton,
                                ui_->resetAllMriViewsButton, ui_->registrationResetAllMriViewsButton})
        button->setStyleSheet(sliceButtonStyle);
    const QString acceptButtonStyle = QStringLiteral(
        "QPushButton { background: #176b87; color: white; border: 1px solid #0f5269; "
        "border-radius: 5px; min-height: 34px; padding: 4px 18px; font-weight: 700; } "
        "QPushButton:hover { background: #2083a3; border-color: #0c4357; } "
        "QPushButton:pressed { background: #10566e; } "
        "QPushButton:disabled { background: #d9e0e4; color: #75838b; border-color: #c4cdd2; }");
    for (QPushButton* button : {ui_->acceptDeviceReadinessButton, ui_->acceptImagingButton,
                                ui_->acceptRegistrationButton}) {
        button->setStyleSheet(acceptButtonStyle);
        button->setMinimumWidth(220);
    }
    for (QLabel* label : findChildren<QLabel*>()) {
        label->setTextInteractionFlags(label->textInteractionFlags() |
                                       Qt::TextSelectableByMouse |
                                       Qt::TextSelectableByKeyboard);
        label->setFocusPolicy(Qt::ClickFocus);
    }
    // Fixed-size resource indicators keep the label origin stable while
    // providing an explicit X for unchecked controls.
    for (QCheckBox* checkBox : findChildren<QCheckBox*>()) {
        checkBox->setStyleSheet(QStringLiteral(
            "QCheckBox { spacing: 6px; padding: 2px 3px; background: transparent; } "
            "QCheckBox::indicator { width: 13px; height: 13px; } "
            "QCheckBox::indicator:unchecked { image: url(:/checkbox/checkbox_unchecked.xpm); } "
            "QCheckBox::indicator:checked { image: url(:/checkbox/checkbox_checked.xpm); }"));
    }
    for (std::size_t i = 0; i < stageCount; ++i) ui_->stageList->addItem(QString());
    // Correction is executed as part of the combined Correction & coupling
    // gate; keep its workflow state internally but do not present a duplicate
    // navigation tab to the operator.
    ui_->stageList->item(static_cast<int>(beam::gui::WorkflowStage::Correction))->setHidden(true);

    // Coupling is kept as a real Qt page (rather than the former generic
    // placeholder) so the operator can measure, review, and accept acoustic
    // transmission before moving on to correction.
    ui_->placeholderText->hide();
    couplingTitleLabel_ = new QLabel(QStringLiteral("Correction & coupling"), ui_->placeholderPage);
    couplingTitleLabel_->setStyleSheet(QStringLiteral("QLabel { color: #f2f7f8; font-size: 20px; font-weight: 700; }"));
    couplingTitleLabel_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    couplingDescriptionLabel_ = new QLabel(
        QStringLiteral("Run the BeamV0 through-transmit measurement. It evaluates correction data and verifies that acoustic coupling is adequate before treatment."),
        ui_->placeholderPage);
    couplingDescriptionLabel_->setWordWrap(true);
    couplingDescriptionLabel_->setStyleSheet(QStringLiteral("QLabel { color: #c8d9de; padding: 4px 0 10px 0; }"));
    couplingStatusLabel_ = new QLabel(QStringLiteral("No coupling measurement has been run."), ui_->placeholderPage);
    couplingStatusLabel_->setWordWrap(true);
    couplingStatusLabel_->setStyleSheet(QStringLiteral(
        "QLabel { color: #dcebef; background: #183944; border: 1px solid #2b6172; border-radius: 5px; padding: 12px; }"));
    couplingProgressBar_ = new QProgressBar(ui_->placeholderPage);
    couplingProgressBar_->setRange(0, 100);
    couplingProgressBar_->setValue(0);
    couplingProgressBar_->setFormat(QStringLiteral("Transmission: %p%"));
    couplingProgressBar_->setMinimumHeight(24);
    couplingProgressBar_->setStyleSheet(QStringLiteral(
        "QProgressBar { color: #eafaff; background: #263a42; border: 1px solid #416b78; border-radius: 4px; text-align: center; } "
        "QProgressBar::chunk { background: #2fa7c7; border-radius: 3px; }"));
    runCouplingCheckButton_ = new QPushButton(QStringLiteral("Run correction / coupling measurement"), ui_->placeholderPage);
    runCouplingCheckButton_->setMinimumHeight(38);
    runCouplingCheckButton_->setStyleSheet(acceptButtonStyle);
    acceptCouplingButton_ = new QPushButton(QStringLiteral("Accept correction & coupling and continue →"), ui_->placeholderPage);
    acceptCouplingButton_->setMinimumHeight(40);
    acceptCouplingButton_->setEnabled(false);
    acceptCouplingButton_->setStyleSheet(acceptButtonStyle);
    ui_->placeholderLayout->setAlignment(Qt::AlignTop);
    ui_->placeholderLayout->addWidget(couplingTitleLabel_);
    ui_->placeholderLayout->addWidget(couplingDescriptionLabel_);
    ui_->placeholderLayout->addSpacing(12);
    ui_->placeholderLayout->addWidget(couplingStatusLabel_);
    ui_->placeholderLayout->addWidget(couplingProgressBar_);
    ui_->placeholderLayout->addWidget(runCouplingCheckButton_);
    ui_->placeholderLayout->addWidget(acceptCouplingButton_);

    correctionTitleLabel_ = new QLabel(QStringLiteral("Correction"), ui_->placeholderPage);
    correctionTitleLabel_->setStyleSheet(QStringLiteral("QLabel { color: #f2f7f8; font-size: 20px; font-weight: 700; }"));
    correctionDescriptionLabel_ = new QLabel(
        QStringLiteral("Run the BeamV0 through-transmit correction measurement, review the transmission level, and accept the correction before treatment planning."),
        ui_->placeholderPage);
    correctionDescriptionLabel_->setWordWrap(true);
    correctionDescriptionLabel_->setStyleSheet(QStringLiteral("QLabel { color: #c8d9de; padding: 4px 0 10px 0; }"));
    correctionStatusLabel_ = new QLabel(QStringLiteral("No correction measurement has been run."), ui_->placeholderPage);
    correctionStatusLabel_->setWordWrap(true);
    correctionStatusLabel_->setStyleSheet(QStringLiteral(
        "QLabel { color: #dcebef; background: #183944; border: 1px solid #2b6172; border-radius: 5px; padding: 12px; }"));
    correctionProgressBar_ = new QProgressBar(ui_->placeholderPage);
    correctionProgressBar_->setRange(0, 100);
    correctionProgressBar_->setValue(0);
    correctionProgressBar_->setFormat(QStringLiteral("Current transmission: %p%"));
    correctionProgressBar_->setMinimumHeight(24);
    correctionProgressBar_->setStyleSheet(QStringLiteral(
        "QProgressBar { color: #eafaff; background: #263a42; border: 1px solid #416b78; border-radius: 4px; text-align: center; } "
        "QProgressBar::chunk { background: #43b581; border-radius: 3px; }"));
    runCorrectionButton_ = new QPushButton(QStringLiteral("Run correction measurement"), ui_->placeholderPage);
    runCorrectionButton_->setMinimumHeight(38);
    runCorrectionButton_->setStyleSheet(acceptButtonStyle);
    acceptCorrectionButton_ = new QPushButton(QStringLiteral("Accept correction and continue →"), ui_->placeholderPage);
    acceptCorrectionButton_->setMinimumHeight(40);
    acceptCorrectionButton_->setEnabled(false);
    acceptCorrectionButton_->setStyleSheet(acceptButtonStyle);
    ui_->placeholderLayout->addWidget(correctionTitleLabel_);
    ui_->placeholderLayout->addWidget(correctionDescriptionLabel_);
    ui_->placeholderLayout->addSpacing(12);
    ui_->placeholderLayout->addWidget(correctionStatusLabel_);
    ui_->placeholderLayout->addWidget(correctionProgressBar_);
    ui_->placeholderLayout->addWidget(runCorrectionButton_);
    ui_->placeholderLayout->addWidget(acceptCorrectionButton_);

    treatmentPlanTitleLabel_ = new QLabel(QStringLiteral("Treatment plan"), ui_->placeholderPage);
    treatmentPlanTitleLabel_->setStyleSheet(QStringLiteral("QLabel { color: #f2f7f8; font-size: 20px; font-weight: 700; }"));
    treatmentPlanDescriptionLabel_ = new QLabel(
        QStringLiteral("Review the registered target and select a treatment protocol. Device output remains disabled until the plan is accepted and Safety Review is complete."),
        ui_->placeholderPage);
    treatmentPlanDescriptionLabel_->setWordWrap(true);
    treatmentPlanDescriptionLabel_->setStyleSheet(QStringLiteral("QLabel { color: #c8d9de; padding: 4px 0 10px 0; }"));
    treatmentPlanTargetLabel_ = new QLabel(QStringLiteral("Registered target: not available"), ui_->placeholderPage);
    treatmentPlanTargetLabel_->setStyleSheet(QStringLiteral(
        "QLabel { color: #dcebef; background: #183944; border: 1px solid #2b6172; border-radius: 5px; padding: 12px; }"));
    treatmentProtocolLabel_ = new QLabel(QStringLiteral("Treatment protocol"), ui_->placeholderPage);
    treatmentProtocolLabel_->setStyleSheet(QStringLiteral("QLabel { color: #f2f7f8; font-weight: 600; padding-top: 8px; }"));
    treatmentProtocolCombo_ = new QComboBox(ui_->placeholderPage);
    treatmentProtocolCombo_->addItems({QStringLiteral("Default"), QStringLiteral("Addiction"),
                                       QStringLiteral("PTSD"), QStringLiteral("PainACC"),
                                       QStringLiteral("PainSCCandAMCC"), QStringLiteral("PainAMCCandSCC")});
    treatmentProtocolCombo_->setMinimumHeight(32);
    treatmentProtocolCombo_->setStyleSheet(QStringLiteral(
        "QComboBox { background: #ffffff; color: #17313f; padding: 5px 8px; border: 1px solid #7c9da8; border-radius: 4px; }"));
    auto configurePlanTable = [](QTableWidget* table) {
        table->setAlternatingRowColors(true);
        table->setStyleSheet(QStringLiteral(
            "QTableWidget { color: #17313f; background: #ffffff; gridline-color: #b7cbd1; } "
            "QTableWidget::item { padding: 4px; } "
            "QHeaderView::section { color: #17313f; background: #dcebef; padding: 5px; font-weight: 600; }"));
        table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        table->verticalHeader()->setVisible(false);
    };
    treatmentTargetTableLabel_ = new QLabel(QStringLiteral("Target list"), ui_->placeholderPage);
    treatmentTargetTableLabel_->setStyleSheet(QStringLiteral("QLabel { color: #f2f7f8; font-weight: 600; padding-top: 8px; }"));
    treatmentTargetTable_ = new QTableWidget(1, 5, ui_->placeholderPage);
    treatmentTargetTable_->setHorizontalHeaderLabels({QStringLiteral("Target"), QStringLiteral("LR (mm)"),
                                                       QStringLiteral("AP (mm)"), QStringLiteral("IS (mm)"),
                                                       QStringLiteral("Status")});
    treatmentTargetTable_->setFixedHeight(78);
    configurePlanTable(treatmentTargetTable_);
    treatmentTargetTable_->setItem(0, 0, new QTableWidgetItem(QStringLiteral("Target 1")));
    treatmentTargetTable_->setItem(0, 1, new QTableWidgetItem(QStringLiteral("0.00")));
    treatmentTargetTable_->setItem(0, 2, new QTableWidgetItem(QStringLiteral("0.00")));
    treatmentTargetTable_->setItem(0, 3, new QTableWidgetItem(QStringLiteral("0.00")));
    treatmentTargetTable_->setItem(0, 4, new QTableWidgetItem(QStringLiteral("Registered")));
    treatmentProtocolTableLabel_ = new QLabel(QStringLiteral("Treatment protocol details"), ui_->placeholderPage);
    treatmentProtocolTableLabel_->setStyleSheet(QStringLiteral("QLabel { color: #f2f7f8; font-weight: 600; padding-top: 8px; }"));
    treatmentProtocolTable_ = new QTableWidget(1, 8, ui_->placeholderPage);
    treatmentProtocolTable_->setHorizontalHeaderLabels({QStringLiteral("Number"), QStringLiteral("Name"),
                                                         QStringLiteral("Duration (s)"), QStringLiteral("Amplitude"),
                                                         QStringLiteral("Parameters"), QStringLiteral("Response Pain"),
                                                         QStringLiteral("Response Mood"), QStringLiteral("Notes")});
    treatmentProtocolTable_->setFixedHeight(86);
    configurePlanTable(treatmentProtocolTable_);
    const QStringList protocolValues = {QStringLiteral("1"), QStringLiteral("SCC1"), QStringLiteral("30"),
                                        QStringLiteral("0.5"), QStringLiteral("X:0,Y:0,Z:0,0.03,0.7,0.005,0.010"),
                                        QStringLiteral("0"), QStringLiteral("0"), QStringLiteral("notes")};
    for (int column = 0; column < protocolValues.size(); ++column)
        treatmentProtocolTable_->setItem(0, column, new QTableWidgetItem(protocolValues.at(column)));
    auto* sonicationActions = new QHBoxLayout;
    addSonicationButton_ = new QPushButton(QStringLiteral("Add sonication"), ui_->placeholderPage);
    removeSonicationButton_ = new QPushButton(QStringLiteral("Remove selected"), ui_->placeholderPage);
    sonicationActions->addWidget(addSonicationButton_);
    sonicationActions->addWidget(removeSonicationButton_);
    sonicationActions->addStretch(1);
    acceptTreatmentPlanButton_ = new QPushButton(QStringLiteral("Accept treatment plan and continue →"), ui_->placeholderPage);
    acceptTreatmentPlanButton_->setMinimumHeight(40);
    acceptTreatmentPlanButton_->setStyleSheet(acceptButtonStyle);
    treatmentTabs_ = new QTabWidget(ui_->placeholderPage);
    treatmentTabs_->setStyleSheet(QStringLiteral(
        "QTabWidget::pane { border: 1px solid #356574; background: #173944; } "
        "QTabBar::tab { color: #dcebef; background: #244b58; padding: 7px 13px; } "
        "QTabBar::tab:selected { background: #176b87; }"));
    treatmentMriPage_ = new QWidget(ui_->placeholderPage);
    auto* treatmentMriLayout = new QVBoxLayout(treatmentMriPage_);
    auto* treatmentMriInstruction = new QLabel(
        QStringLiteral("Select a target row, then click a location in any MRI plane. The selected target is updated in RAS coordinates and shown in all three planes."),
        treatmentMriPage_);
    treatmentMriInstruction->setWordWrap(true);
    treatmentMriInstruction->setStyleSheet(QStringLiteral("QLabel { color: #dcebef; padding: 4px; }"));
    treatmentMriLayout->addWidget(treatmentMriInstruction);
    auto* treatmentViewerControls = new QHBoxLayout;
    auto* treatmentShowField = new QCheckBox(QStringLiteral("Show field"), treatmentMriPage_);
    auto* treatmentShowTarget = new QCheckBox(QStringLiteral("Show target"), treatmentMriPage_);
    auto* treatmentShowTransducers = new QCheckBox(QStringLiteral("Show transducers"), treatmentMriPage_);
    auto* treatmentShowFiducials = new QCheckBox(QStringLiteral("Show fiducials"), treatmentMriPage_);
    auto* treatmentShowNavigation = new QCheckBox(QStringLiteral("Linked navigation"), treatmentMriPage_);
    for (QCheckBox* check : {treatmentShowField, treatmentShowTarget, treatmentShowTransducers,
                             treatmentShowFiducials, treatmentShowNavigation}) {
        check->setStyleSheet(QStringLiteral("QCheckBox { color: #dcebef; padding: 2px; }"));
        treatmentViewerControls->addWidget(check);
    }
    auto* treatmentBrightnessDown = new QToolButton(treatmentMriPage_);
    treatmentBrightnessDown->setText(QStringLiteral("▼"));
    treatmentBrightnessDown->setToolTip(QStringLiteral("Decrease brightness"));
    auto* treatmentBrightnessUp = new QToolButton(treatmentMriPage_);
    treatmentBrightnessUp->setText(QStringLiteral("▲"));
    treatmentBrightnessUp->setToolTip(QStringLiteral("Increase brightness"));
    auto* treatmentBrightnessReset = new QToolButton(treatmentMriPage_);
    treatmentBrightnessReset->setText(QStringLiteral("Reset"));
    treatmentBrightnessReset->setToolTip(QStringLiteral("Reset MRI view brightness"));
    treatmentViewerControls->addWidget(treatmentBrightnessDown);
    treatmentViewerControls->addWidget(treatmentBrightnessUp);
    treatmentViewerControls->addWidget(treatmentBrightnessReset);
    auto* treatmentTransparencyLabel = new QLabel(QStringLiteral("Transducer transparency"), treatmentMriPage_);
    auto* treatmentTransparency = new QSlider(Qt::Horizontal, treatmentMriPage_);
    treatmentTransparency->setRange(0, 100);
    treatmentTransparency->setValue(ui_->transducerTransparencySlider->value());
    treatmentTransparency->setFixedWidth(110);
    treatmentViewerControls->addWidget(treatmentTransparencyLabel);
    treatmentViewerControls->addWidget(treatmentTransparency);
    treatmentViewerControls->addStretch(1);
    treatmentMriLayout->addLayout(treatmentViewerControls);
    connect(treatmentShowField, &QCheckBox::toggled, ui_->showFieldCheckBox, &QCheckBox::setChecked);
    connect(treatmentShowTarget, &QCheckBox::toggled, ui_->showTargetCheckBox, &QCheckBox::setChecked);
    connect(treatmentShowTransducers, &QCheckBox::toggled, ui_->showTransducersCheckBox, &QCheckBox::setChecked);
    connect(treatmentShowFiducials, &QCheckBox::toggled, ui_->showFiducialsCheckBox, &QCheckBox::setChecked);
    connect(treatmentShowNavigation, &QCheckBox::toggled, ui_->showLinkedNavigationCheckBox, &QCheckBox::setChecked);
    connect(treatmentShowNavigation, &QCheckBox::toggled, this, [this](bool visible) {
        for (WorkflowMriView* view : {treatmentSagittalPreview_, treatmentCoronalPreview_, treatmentAxialPreview_})
            view->setNavigationCrosshairVisible(visible);
    });
    connect(ui_->showFieldCheckBox, &QCheckBox::toggled, treatmentShowField, &QCheckBox::setChecked);
    connect(ui_->showTargetCheckBox, &QCheckBox::toggled, treatmentShowTarget, &QCheckBox::setChecked);
    connect(ui_->showTransducersCheckBox, &QCheckBox::toggled, treatmentShowTransducers, &QCheckBox::setChecked);
    connect(ui_->showFiducialsCheckBox, &QCheckBox::toggled, treatmentShowFiducials, &QCheckBox::setChecked);
    connect(ui_->showLinkedNavigationCheckBox, &QCheckBox::toggled, treatmentShowNavigation, &QCheckBox::setChecked);
    connect(treatmentTransparency, &QSlider::valueChanged, ui_->transducerTransparencySlider, &QSlider::setValue);
    connect(ui_->transducerTransparencySlider, &QSlider::valueChanged, treatmentTransparency, &QSlider::setValue);
    connect(treatmentBrightnessDown, &QToolButton::clicked, ui_->brightnessDownButton, &QToolButton::click);
    connect(treatmentBrightnessUp, &QToolButton::clicked, ui_->brightnessUpButton, &QToolButton::click);
    connect(treatmentBrightnessReset, &QToolButton::clicked, ui_->resetAllMriViewsButton, &QToolButton::click);
    treatmentShowField->setChecked(ui_->showFieldCheckBox->isChecked());
    treatmentShowTarget->setChecked(ui_->showTargetCheckBox->isChecked());
    treatmentShowTransducers->setChecked(ui_->showTransducersCheckBox->isChecked());
    treatmentShowFiducials->setChecked(ui_->showFiducialsCheckBox->isChecked());
    treatmentShowNavigation->setChecked(ui_->showLinkedNavigationCheckBox->isChecked());
    auto makeTreatmentView = [this](const QString& title, WorkflowMriView*& view, QSlider*& slider) {
        auto* group = new QGroupBox(title, treatmentMriPage_);
        group->setStyleSheet(QStringLiteral("QGroupBox { color: #dcebef; font-weight: 600; }"));
        auto* layout = new QVBoxLayout(group);
        view = new WorkflowMriView(group);
        view->setMinimumSize(210, 190);
        view->setPointPlacementEnabled(true);
        slider = new QSlider(Qt::Horizontal, group);
        slider->setEnabled(false);
        layout->addWidget(view, 1);
        layout->addWidget(slider);
        return group;
    };
    auto* treatmentMriViews = new QHBoxLayout;
    treatmentMriViews->addWidget(makeTreatmentView(QStringLiteral("Sagittal (LR)"), treatmentSagittalPreview_, treatmentSagittalSlider_), 1);
    treatmentMriViews->addWidget(makeTreatmentView(QStringLiteral("Coronal (AP)"), treatmentCoronalPreview_, treatmentCoronalSlider_), 1);
    treatmentMriViews->addWidget(makeTreatmentView(QStringLiteral("Axial (IS)"), treatmentAxialPreview_, treatmentAxialSlider_), 1);
    treatmentMriLayout->addLayout(treatmentMriViews, 1);
    treatmentControlsPanel_ = new QWidget(ui_->placeholderPage);
    auto* treatmentControlsLayout = new QVBoxLayout(treatmentControlsPanel_);
    treatmentControlsLayout->setContentsMargins(4, 4, 4, 4);
    treatmentControlsLayout->addWidget(treatmentPlanTargetLabel_);
    auto* targetAndStimTables = new QHBoxLayout;
    auto* targetPanel = new QVBoxLayout;
    targetPanel->addWidget(treatmentTargetTableLabel_);
    targetPanel->addWidget(treatmentTargetTable_);
    targetPanel->addWidget(treatmentProtocolLabel_);
    targetPanel->addWidget(treatmentProtocolCombo_);
    targetAndStimTables->addLayout(targetPanel, 1);
    auto* stimulationPanel = new QVBoxLayout;
    auto* stimulationTable = new QTableWidget(8, 2, treatmentControlsPanel_);
    stimulationTable->setHorizontalHeaderLabels({QStringLiteral("Parameter"), QStringLiteral("Value")});
    configurePlanTable(stimulationTable);
    const QStringList stimNames = {QStringLiteral("Amplitude"), QStringLiteral("Start time (s)"),
                                   QStringLiteral("End time (s)"), QStringLiteral("Burst duration BD (s)"),
                                   QStringLiteral("Burst interval BI (s)"), QStringLiteral("Pulse duration PD (s)"),
                                   QStringLiteral("Pulse interval PI (s)"), QStringLiteral("Target")};
    const QStringList stimValues = {QStringLiteral("0.75"), QStringLiteral("0"), QStringLiteral("30"),
                                    QStringLiteral("0.03"), QStringLiteral("0.7"), QStringLiteral("0.005"),
                                    QStringLiteral("0.01"), QStringLiteral("Target 1")};
    for (int row = 0; row < stimNames.size(); ++row) {
        stimulationTable->setItem(row, 0, new QTableWidgetItem(stimNames.at(row)));
        stimulationTable->setItem(row, 1, new QTableWidgetItem(stimValues.at(row)));
    }
    stimulationPanel->addWidget(new QLabel(QStringLiteral("Stim parameters"), treatmentControlsPanel_));
    stimulationPanel->addWidget(stimulationTable);
    targetAndStimTables->addLayout(stimulationPanel, 1);
    treatmentControlsLayout->addLayout(targetAndStimTables);
    treatmentControlsLayout->addWidget(treatmentProtocolTableLabel_);
    treatmentControlsLayout->addWidget(treatmentProtocolTable_);
    treatmentControlsLayout->addLayout(sonicationActions);
    treatmentControlsLayout->addWidget(acceptTreatmentPlanButton_);
    auto* pulsePage = new QWidget(treatmentTabs_);
    auto* pulseLayout = new QVBoxLayout(pulsePage);
    auto* pulseTable = new QTableWidget(4, 2, pulsePage);
    pulseTable->setHorizontalHeaderLabels({QStringLiteral("Pulse detail"), QStringLiteral("Value")});
    configurePlanTable(pulseTable);
    for (int row = 0; row < 4; ++row) {
        pulseTable->setItem(row, 0, new QTableWidgetItem(QStringList{QStringLiteral("Frequency"), QStringLiteral("Cycles"), QStringLiteral("Duty cycle"), QStringLiteral("Power limit")}.at(row)));
        pulseTable->setItem(row, 1, new QTableWidgetItem(QStringList{QStringLiteral("650 kHz"), QStringLiteral("As protocol"), QStringLiteral("As protocol"), QStringLiteral("Safety limited")}.at(row)));
    }
    pulseLayout->addWidget(pulseTable);
    treatmentTabs_->addTab(pulsePage, QStringLiteral("Pulse details"));
    auto* targetingPage = new QWidget(treatmentTabs_);
    auto* targetingLayout = new QVBoxLayout(targetingPage);
    targetingLayout->addWidget(new TargetingExampleCanvas);
    treatmentTabs_->addTab(targetingPage, QStringLiteral("Targeting examples"));
    treatmentBodySplitter_ = new QSplitter(Qt::Vertical, ui_->placeholderPage);
    auto* treatmentBodySplitter = static_cast<QSplitter*>(treatmentBodySplitter_);
    treatmentBodySplitter->setChildrenCollapsible(false);
    treatmentBodySplitter->addWidget(treatmentMriPage_);
    auto* treatmentExtras = new QWidget(ui_->placeholderPage);
    auto* treatmentExtrasLayout = new QVBoxLayout(treatmentExtras);
    treatmentExtrasLayout->setContentsMargins(0, 0, 0, 0);
    treatmentExtrasLayout->addWidget(treatmentControlsPanel_, 1);
    treatmentExtrasLayout->addWidget(treatmentTabs_, 1);
    treatmentBodySplitter->addWidget(treatmentExtras);
    treatmentBodySplitter->setStretchFactor(0, 3);
    treatmentBodySplitter->setStretchFactor(1, 2);
    treatmentBodySplitter->setSizes({520, 360});
    ui_->placeholderLayout->addWidget(treatmentPlanTitleLabel_);
    ui_->placeholderLayout->addWidget(treatmentPlanDescriptionLabel_);
    ui_->placeholderLayout->addWidget(treatmentBodySplitter, 1);

    treatmentExecutionTitleLabel_ = new QLabel(QStringLiteral("Treatment execution"), ui_->placeholderPage);
    treatmentExecutionTitleLabel_->setStyleSheet(QStringLiteral("QLabel { color: #f2f7f8; font-size: 20px; font-weight: 700; }"));
    treatmentExecutionDescriptionLabel_ = new QLabel(
        QStringLiteral("Execute the accepted sonication plan. Hardware output remains simulated until a Beam device is connected."),
        ui_->placeholderPage);
    treatmentExecutionDescriptionLabel_->setWordWrap(true);
    treatmentExecutionDescriptionLabel_->setStyleSheet(QStringLiteral("QLabel { color: #c8d9de; padding: 4px 0 10px 0; }"));
    treatmentExecutionStatusLabel_ = new QLabel(QStringLiteral("Treatment has not started."), ui_->placeholderPage);
    treatmentExecutionStatusLabel_->setStyleSheet(QStringLiteral(
        "QLabel { color: #dcebef; background: #183944; border: 1px solid #2b6172; border-radius: 5px; padding: 12px; }"));
    treatmentExecutionProgressBar_ = new QProgressBar(ui_->placeholderPage);
    treatmentExecutionProgressBar_->setRange(0, 100);
    treatmentExecutionProgressBar_->setValue(0);
    treatmentExecutionProgressBar_->setFormat(QStringLiteral("Treatment progress: %p%"));
    treatmentExecutionProgressBar_->setMinimumHeight(24);
    startTreatmentButton_ = new QPushButton(QStringLiteral("Start treatment (simulated)"), ui_->placeholderPage);
    startTreatmentButton_->setMinimumHeight(40);
    startTreatmentButton_->setStyleSheet(acceptButtonStyle);
    abortTreatmentButton_ = new QPushButton(QStringLiteral("Abort treatment"), ui_->placeholderPage);
    abortTreatmentButton_->setMinimumHeight(36);
    abortTreatmentButton_->setEnabled(false);
    ui_->placeholderLayout->addWidget(treatmentExecutionTitleLabel_);
    ui_->placeholderLayout->addWidget(treatmentExecutionDescriptionLabel_);
    ui_->placeholderLayout->addWidget(treatmentExecutionStatusLabel_);
    ui_->placeholderLayout->addWidget(treatmentExecutionProgressBar_);
    ui_->placeholderLayout->addWidget(startTreatmentButton_);
    ui_->placeholderLayout->addWidget(abortTreatmentButton_);

    safetyReviewTitleLabel_ = new QLabel(QStringLiteral("Safety Review"), ui_->placeholderPage);
    safetyReviewTitleLabel_->setStyleSheet(QStringLiteral("QLabel { color: #f2f7f8; font-size: 20px; font-weight: 700; }"));
    safetyReviewDescriptionLabel_ = new QLabel(
        QStringLiteral("Confirm the required safety gates before treatment. This review does not send device output."),
        ui_->placeholderPage);
    safetyReviewDescriptionLabel_->setWordWrap(true);
    safetyReviewDescriptionLabel_->setStyleSheet(QStringLiteral("QLabel { color: #c8d9de; padding: 4px 0 10px 0; }"));
    safetyRegistrationCheckBox_ = new QCheckBox(QStringLiteral("MRI registration accepted"), ui_->placeholderPage);
    safetyCorrectionCheckBox_ = new QCheckBox(QStringLiteral("Correction and coupling accepted"), ui_->placeholderPage);
    safetyPlanCheckBox_ = new QCheckBox(QStringLiteral("Treatment plan accepted"), ui_->placeholderPage);
    for (QCheckBox* check : {safetyRegistrationCheckBox_, safetyCorrectionCheckBox_, safetyPlanCheckBox_}) {
        check->setEnabled(false);
        check->setStyleSheet(QStringLiteral("QCheckBox { color: #f2f7f8; padding: 6px; }"));
    }
    safetyReviewStatusLabel_ = new QLabel(QStringLiteral("Review not yet completed."), ui_->placeholderPage);
    safetyReviewStatusLabel_->setStyleSheet(QStringLiteral(
        "QLabel { color: #dcebef; background: #183944; border: 1px solid #2b6172; border-radius: 5px; padding: 12px; }"));
    acceptSafetyReviewButton_ = new QPushButton(QStringLiteral("Accept safety review and continue →"), ui_->placeholderPage);
    acceptSafetyReviewButton_->setMinimumHeight(40);
    acceptSafetyReviewButton_->setStyleSheet(acceptButtonStyle);
    ui_->placeholderLayout->addWidget(safetyReviewTitleLabel_);
    ui_->placeholderLayout->addWidget(safetyReviewDescriptionLabel_);
    ui_->placeholderLayout->addWidget(safetyRegistrationCheckBox_);
    ui_->placeholderLayout->addWidget(safetyCorrectionCheckBox_);
    ui_->placeholderLayout->addWidget(safetyPlanCheckBox_);
    ui_->placeholderLayout->addWidget(safetyReviewStatusLabel_);
    ui_->placeholderLayout->addWidget(acceptSafetyReviewButton_);
    ui_->placeholderLayout->addStretch(1);

    connect(ui_->stageList, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row >= 0 && row < static_cast<int>(stageCount)) selectStage(static_cast<beam::gui::WorkflowStage>(row));
    });
    connect(ui_->completeStageButton, &QPushButton::clicked, this, [this] { completeCurrentStage(); });
    connect(ui_->acceptCaseButton, &QPushButton::clicked, this, [this] { completeCurrentStage(); });
    connect(runCouplingCheckButton_, &QPushButton::clicked, this, [this] {
        // Use the same measurement path as the Correction action below; the
        // result is both the correction measurement and the coupling check.
        runCorrectionButton_->click();
        couplingCheckPassed_ = correctionCheckPassed_;
        workflow_.change(beam::gui::WorkflowStage::Coupling,
                         "Correction/through-transmit measured; awaiting operator acceptance.");
        couplingProgressBar_->setValue(correctionProgressBar_->value());
        couplingStatusLabel_->setText(correctionStatusLabel_->text());
        acceptCouplingButton_->setEnabled(couplingCheckPassed_);
        refresh();
    });
    connect(acceptCouplingButton_, &QPushButton::clicked, this, [this] {
        if (!couplingCheckPassed_) {
            showMessage(QStringLiteral("Run and pass the coupling check before accepting it."), true);
            return;
        }
        std::string reason;
        if (workflow_.state(beam::gui::WorkflowStage::Coupling).status != beam::gui::WorkflowStatus::Complete &&
            !workflow_.complete(beam::gui::WorkflowStage::Coupling, &reason)) {
            showMessage(QString::fromStdString(reason), true);
            return;
        }
        if (workflow_.state(beam::gui::WorkflowStage::Correction).status != beam::gui::WorkflowStatus::Complete &&
            !workflow_.complete(beam::gui::WorkflowStage::Correction, &reason)) {
            showMessage(QString::fromStdString(reason), true);
            return;
        }
        acceptCouplingButton_->setEnabled(false);
        runCouplingCheckButton_->setEnabled(false);
        showMessage(QStringLiteral("Correction and coupling accepted. Continuing to Treatment plan."), false);
        refresh();
        ui_->stageList->setCurrentRow(static_cast<int>(workflow_.nextStage()));
    });
    connect(runCorrectionButton_, &QPushButton::clicked, this, [this] {
        double transmission = 0.88;
        bool simulated = true;
        const auto ports = beam::serialcom::listAvailableComPorts();
        if (!ports.empty()) {
            try {
                beam::serialcom::SerialPort link(ports.front());
                beam::serialcom::sendSerialCommand(link, "Correction");
                std::vector<double> raw;
                for (int lineCount = 0; lineCount < 100000 && link.bytesAvailable() > 0; ++lineCount) {
                    const std::string line = link.readLine();
                    if (line.empty()) break;
                    const auto values = beam::correction::parseCorrectionWaveformLine(line);
                    raw.insert(raw.end(), values.begin(), values.end());
                }
                const auto split = beam::correction::splitAndFilterReceiveWaveform(raw);
                const auto measured = beam::correction::throughTransmitAmplitude(split.ch0rcv, split.ch1rcv);
                transmission = measured.amp;
                simulated = false;
            } catch (const std::exception&) {
                // A stale or unrelated COM port should not prevent offline
                // workflow testing when no Beam device is connected.
                simulated = true;
                transmission = 0.88;
            }
        }
        const int percent = std::clamp(static_cast<int>(std::lround(transmission * 100.0)), 0, 100);
        correctionCheckPassed_ = transmission >= 0.07;
        correctionProgressBar_->setValue(percent);
        correctionStatusLabel_->setText(QStringLiteral(
            "Correction measurement %1: current transmission %2%. The through-transmit level is %3 the safety threshold.")
                .arg(simulated ? QStringLiteral("(simulated)") : QStringLiteral("from device"))
                .arg(percent)
                .arg(correctionCheckPassed_ ? QStringLiteral("above") : QStringLiteral("below")));
        workflow_.change(beam::gui::WorkflowStage::Correction,
                         "Correction measured; awaiting operator acceptance.");
        acceptCorrectionButton_->setEnabled(correctionCheckPassed_);
        showMessage(correctionCheckPassed_ ? QStringLiteral("Correction measurement passed. Accept correction to continue.")
                                           : QStringLiteral("Correction measurement failed the transmission threshold."),
                    !correctionCheckPassed_);
        refresh();
    });
    connect(acceptCorrectionButton_, &QPushButton::clicked, this, [this] {
        if (!correctionCheckPassed_) {
            showMessage(QStringLiteral("Run the correction measurement before accepting it."), true);
            return;
        }
        std::string reason;
        if (!workflow_.complete(beam::gui::WorkflowStage::Correction, &reason)) {
            showMessage(QString::fromStdString(reason), true);
            return;
        }
        acceptCorrectionButton_->setEnabled(false);
        runCorrectionButton_->setEnabled(false);
        showMessage(QStringLiteral("Correction and coupling accepted. Continuing to Treatment plan."), false);
        refresh();
        ui_->stageList->setCurrentRow(static_cast<int>(workflow_.nextStage()));
    });
    connect(acceptTreatmentPlanButton_, &QPushButton::clicked, this, [this] {
        std::string reason;
        if (!workflow_.complete(beam::gui::WorkflowStage::TreatmentPlan, &reason)) {
            showMessage(QString::fromStdString(reason), true);
            return;
        }
        acceptTreatmentPlanButton_->setEnabled(false);
        showMessage(QStringLiteral("Treatment plan accepted. Continuing to Safety Review."), false);
        refresh();
        ui_->stageList->setCurrentRow(static_cast<int>(workflow_.nextStage()));
    });
    connect(addSonicationButton_, &QPushButton::clicked, this, [this] {
        const int row = treatmentProtocolTable_->rowCount();
        treatmentProtocolTable_->insertRow(row);
        const QStringList values = {QString::number(row + 1), QStringLiteral("SCC%1").arg(row + 1), QStringLiteral("30"),
                                    QStringLiteral("0.75"), QStringLiteral("X:0,Y:0,Z:0,0.03,0.7,0.005,0.010"),
                                    QStringLiteral("0"), QStringLiteral("0"), QStringLiteral("notes")};
        for (int column = 0; column < values.size(); ++column)
            treatmentProtocolTable_->setItem(row, column, new QTableWidgetItem(values.at(column)));
        treatmentProtocolTable_->selectRow(row);
    });
    connect(removeSonicationButton_, &QPushButton::clicked, this, [this] {
        const int row = treatmentProtocolTable_->currentRow();
        if (row >= 0 && treatmentProtocolTable_->rowCount() > 1)
            treatmentProtocolTable_->removeRow(row);
    });
    const auto parseBeamCsvLine = [](const QString& line) {
        QStringList fields;
        QString field;
        bool quoted = false;
        for (int i = 0; i < line.size(); ++i) {
            const QChar ch = line.at(i);
            if (ch == QLatin1Char('"')) {
                if (quoted && i + 1 < line.size() && line.at(i + 1) == QLatin1Char('"')) {
                    field += QLatin1Char('"');
                    ++i;
                } else {
                    quoted = !quoted;
                }
            } else if (ch == QLatin1Char(',') && !quoted) {
                fields.push_back(field.trimmed());
                field.clear();
            } else {
                field += ch;
            }
        }
        fields.push_back(field.trimmed());
        return fields;
    };
    const auto loadBeamProtocol = [this, parseBeamCsvLine](const QString& protocolName) {
        QString fileName = protocolName + QStringLiteral(".csv");
        const QString relative = QStringLiteral("BeamV0/GUIMatlab/BEAM/GUI/SonicationTab/TreatmentProtocols/") + fileName;
        const QStringList candidates = {
            QDir::current().filePath(relative),
            QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("../../../../") + relative),
            QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("../../../") + relative)};
        QString selectedPath;
        for (const QString& candidate : candidates) {
            if (QFileInfo::exists(candidate)) {
                selectedPath = candidate;
                break;
            }
        }

        QList<QStringList> sourceRows;
        if (!selectedPath.isEmpty()) {
            QFile file(selectedPath);
            if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                QTextStream stream(&file);
                while (!stream.atEnd()) {
                    const QString line = stream.readLine();
                    if (line.trimmed().isEmpty()) continue;
                    const QStringList fields = parseBeamCsvLine(line);
                    if (!fields.isEmpty() && fields.first().compare(QStringLiteral("Number"), Qt::CaseInsensitive) != 0)
                        sourceRows.push_back(fields);
                }
            }
        }
        // Keep offline/release builds usable if the source checkout is not beside
        // the executable. These defaults mirror BeamV0's table schema.
        if (sourceRows.isEmpty()) {
            const int rows = protocolName == QStringLiteral("Default") ? 1 :
                             (protocolName == QStringLiteral("PainACC") || protocolName.contains(QStringLiteral("SCCandAMCC")) ||
                              protocolName.contains(QStringLiteral("AMCCandSCC")) ? 12 : 6);
            for (int row = 0; row < rows; ++row)
                sourceRows.push_back({QString::number(row + 1), QStringLiteral("SCC%1").arg(row + 1), QStringLiteral("30"),
                                      QStringLiteral("0.75"), QStringLiteral("X:0,Y:0,Z:0,0.03,0.7,0.005,0.010"), QStringLiteral("N"), QStringLiteral("notes")});
        }
        treatmentProtocolTable_->setRowCount(sourceRows.size());
        QSet<QString> targetNames;
        for (int row = 0; row < sourceRows.size(); ++row) {
            const QStringList source = sourceRows.at(row);
            const QStringList values = {source.value(0), source.value(1), source.value(2), source.value(3),
                                        source.value(4), source.value(5).compare(QStringLiteral("N"), Qt::CaseInsensitive) == 0 ? QStringLiteral("0") : source.value(5),
                                        source.value(5).compare(QStringLiteral("N"), Qt::CaseInsensitive) == 0 ? QStringLiteral("0") : source.value(5), source.value(6)};
            targetNames.insert(source.value(1));
            for (int column = 0; column < values.size(); ++column)
                treatmentProtocolTable_->setItem(row, column, new QTableWidgetItem(values.at(column)));
        }
        treatmentTargetTable_->setRowCount(targetNames.size());
        QStringList sortedTargets = targetNames.values();
        std::sort(sortedTargets.begin(), sortedTargets.end());
        for (int row = 0; row < sortedTargets.size(); ++row) {
            treatmentTargetTable_->setItem(row, 0, new QTableWidgetItem(sortedTargets.at(row)));
            treatmentTargetTable_->setItem(row, 1, new QTableWidgetItem(QStringLiteral("0.00")));
            treatmentTargetTable_->setItem(row, 2, new QTableWidgetItem(QStringLiteral("0.00")));
            treatmentTargetTable_->setItem(row, 3, new QTableWidgetItem(QStringLiteral("0.00")));
            treatmentTargetTable_->setItem(row, 4, new QTableWidgetItem(QStringLiteral("Available")));
        }
    };
    connect(treatmentProtocolCombo_, &QComboBox::currentTextChanged, this, loadBeamProtocol);
    loadBeamProtocol(treatmentProtocolCombo_->currentText());
    connect(startTreatmentButton_, &QPushButton::clicked, this, [this] {
        std::string reason;
        if (!workflow_.begin(beam::gui::WorkflowStage::Treatment, &reason)) {
            showMessage(QString::fromStdString(reason), true);
            return;
        }
        const int sonicationCount = treatmentProtocolTable_ ? treatmentProtocolTable_->rowCount() : 0;
        treatmentExecutionProgressBar_->setValue(100);
        treatmentExecutionStatusLabel_->setText(QStringLiteral(
            "Treatment completed (simulated): %1 planned sonication%2 processed. No device was connected; no acoustic output was delivered.")
                .arg(sonicationCount).arg(sonicationCount == 1 ? QString() : QStringLiteral("s")));
        startTreatmentButton_->setEnabled(false);
        abortTreatmentButton_->setEnabled(false);
        if (!workflow_.complete(beam::gui::WorkflowStage::Treatment, &reason)) {
            showMessage(QString::fromStdString(reason), true);
            return;
        }
        showMessage(QStringLiteral("Treatment simulation completed. Continuing to Report."), false);
        refresh();
        ui_->stageList->setCurrentRow(static_cast<int>(workflow_.nextStage()));
    });
    connect(abortTreatmentButton_, &QPushButton::clicked, this, [this] {
        workflow_.block(beam::gui::WorkflowStage::Treatment, "Treatment aborted by operator.");
        abortTreatmentButton_->setEnabled(false);
        startTreatmentButton_->setEnabled(true);
        treatmentExecutionStatusLabel_->setText(QStringLiteral("Treatment aborted by operator."));
        showMessage(QStringLiteral("Treatment aborted."), true);
        refresh();
    });
    connect(acceptSafetyReviewButton_, &QPushButton::clicked, this, [this] {
        std::string reason;
        if (!workflow_.complete(beam::gui::WorkflowStage::SafetyReview, &reason)) {
            showMessage(QString::fromStdString(reason), true);
            return;
        }
        acceptSafetyReviewButton_->setEnabled(false);
        safetyReviewStatusLabel_->setText(QStringLiteral("Safety review accepted. Treatment may now begin."));
        showMessage(QStringLiteral("Safety review accepted. Continuing to Treatment."), false);
        refresh();
        ui_->stageList->setCurrentRow(static_cast<int>(workflow_.nextStage()));
    });
    connect(ui_->simulatePassButton, &QPushButton::clicked, this, [this] {
        deviceCheckPassed_ = true;
        workflow_.change(beam::gui::WorkflowStage::SystemCheck,
                         "Device readiness check passed; awaiting operator acceptance.");
        ui_->deviceResult->setText(QStringLiteral("Device responded correctly (simulated). Review and accept readiness."));
        ui_->deviceHeader->setText(QStringLiteral("● Device ready (simulated)"));
        ui_->acceptDeviceReadinessButton->setEnabled(true);
        showMessage(QStringLiteral("System check passed. Accept readiness to continue."), false);
        refresh();
        updateRegistrationAvailability();
    });
    connect(ui_->simulateFailButton, &QPushButton::clicked, this, [this] {
        deviceCheckPassed_ = false;
        ui_->acceptDeviceReadinessButton->setEnabled(false);
        workflow_.block(beam::gui::WorkflowStage::SystemCheck, "Device did not respond (simulated).");
        ui_->deviceResult->setText(QStringLiteral("Device did not respond (simulated)."));
        ui_->deviceHeader->setText(QStringLiteral("● Device fault (simulated)"));
        showMessage(QStringLiteral("System check blocked: device did not respond."), true);
        refresh();
    });
    connect(ui_->acceptDeviceReadinessButton, &QPushButton::clicked, this, [this] {
        if (!deviceCheckPassed_) {
            showMessage(QStringLiteral("Run and pass the device readiness check before accepting it."), true);
            return;
        }
        std::string reason;
        if (!workflow_.complete(beam::gui::WorkflowStage::SystemCheck, &reason)) {
            showMessage(QString::fromStdString(reason), true);
            return;
        }
        ui_->acceptDeviceReadinessButton->setEnabled(false);
        showMessage(QStringLiteral("Device readiness accepted. Continuing to Imaging."), false);
        refresh();
        ui_->stageList->setCurrentRow(static_cast<int>(workflow_.nextStage()));
    });
    connect(ui_->loadNiftiButton, &QPushButton::clicked, this, [this] { chooseNifti(); });
    connect(ui_->loadDicomButton, &QPushButton::clicked, this, [this] { chooseDicomDirectory(); });
    connect(ui_->loadBeamSessionButton, &QPushButton::clicked, this, [this] { chooseBeamSession(); });
    connect(ui_->showFiducialsCheckBox, &QCheckBox::toggled, this, [this] { if (mriLoaded_) showMriPreviews(); });
    connect(ui_->registrationShowFiducialsCheckBox, &QCheckBox::toggled, this, [this] { if (mriLoaded_) showMriPreviews(); });
    connect(ui_->showLinkedNavigationCheckBox, &QCheckBox::toggled, this, [this](bool visible) {
        for (WorkflowMriView* view : {ui_->sagittalPreview, ui_->coronalPreview, ui_->axialPreview})
            view->setNavigationCrosshairVisible(visible);
        for (WorkflowMriView* view : {treatmentSagittalPreview_, treatmentCoronalPreview_, treatmentAxialPreview_})
            view->setNavigationCrosshairVisible(visible);
    });
    connect(ui_->registrationShowLinkedNavigationCheckBox, &QCheckBox::toggled, this, [this](bool visible) {
        for (WorkflowMriView* view : {ui_->registrationSagittalPreview, ui_->registrationCoronalPreview,
                                     ui_->registrationAxialPreview})
            view->setNavigationCrosshairVisible(visible);
    });
    connect(ui_->showLinkedNavigationCheckBox, &QCheckBox::toggled,
            ui_->registrationShowLinkedNavigationCheckBox, &QCheckBox::setChecked);
    connect(ui_->registrationShowLinkedNavigationCheckBox, &QCheckBox::toggled,
            ui_->showLinkedNavigationCheckBox, &QCheckBox::setChecked);
    const auto adjustAllViewBrightness = [this](double amount) {
        for (WorkflowMriView* view : {ui_->sagittalPreview, ui_->coronalPreview, ui_->axialPreview,
                                     ui_->registrationSagittalPreview, ui_->registrationCoronalPreview,
                                     ui_->registrationAxialPreview})
            view->adjustBrightness(amount);
    };
    for (QToolButton* button : {ui_->brightnessDownButton, ui_->registrationBrightnessDownButton})
        connect(button, &QToolButton::clicked, this, [adjustAllViewBrightness] { adjustAllViewBrightness(-0.1); });
    for (QToolButton* button : {ui_->brightnessUpButton, ui_->registrationBrightnessUpButton})
        connect(button, &QToolButton::clicked, this, [adjustAllViewBrightness] { adjustAllViewBrightness(0.1); });
    const auto resetAllViewBrightness = [this] {
        for (WorkflowMriView* view : {ui_->sagittalPreview, ui_->coronalPreview, ui_->axialPreview,
                                     ui_->registrationSagittalPreview, ui_->registrationCoronalPreview,
                                     ui_->registrationAxialPreview})
            view->resetBrightness();
    };
    connect(ui_->resetAllMriViewsButton, &QToolButton::clicked, this, resetAllViewBrightness);
    connect(ui_->registrationResetAllMriViewsButton, &QToolButton::clicked, this, resetAllViewBrightness);
    connect(ui_->showFieldCheckBox, &QCheckBox::toggled, this, [this] { if (mriLoaded_) showMriPreviews(); });
    connect(ui_->showTargetCheckBox, &QCheckBox::toggled, this, [this] { if (mriLoaded_) showMriPreviews(); });
    connect(ui_->showTransducersCheckBox, &QCheckBox::toggled, this, [this] { if (mriLoaded_) showMriPreviews(); });
    connect(ui_->transducerTransparencySlider, &QSlider::valueChanged, this, [this] { if (mriLoaded_) showMriPreviews(); });
    connect(ui_->showFieldCheckBox, &QCheckBox::toggled, ui_->registrationShowFieldCheckBox, &QCheckBox::setChecked);
    connect(ui_->showTargetCheckBox, &QCheckBox::toggled, ui_->registrationShowTargetCheckBox, &QCheckBox::setChecked);
    connect(ui_->showTransducersCheckBox, &QCheckBox::toggled, ui_->registrationShowTransducersCheckBox, &QCheckBox::setChecked);
    connect(ui_->transducerTransparencySlider, &QSlider::valueChanged, ui_->registrationTransparencySlider, &QSlider::setValue);
    connect(ui_->registrationShowFieldCheckBox, &QCheckBox::toggled, ui_->showFieldCheckBox, &QCheckBox::setChecked);
    connect(ui_->registrationShowTargetCheckBox, &QCheckBox::toggled, ui_->showTargetCheckBox, &QCheckBox::setChecked);
    connect(ui_->registrationShowTransducersCheckBox, &QCheckBox::toggled, ui_->showTransducersCheckBox, &QCheckBox::setChecked);
    connect(ui_->registrationTransparencySlider, &QSlider::valueChanged, ui_->transducerTransparencySlider, &QSlider::setValue);
    connect(ui_->sagittalSlider, &QSlider::valueChanged, this, [this] { if (mriLoaded_) showMriPreviews(); });
    connect(ui_->coronalSlider, &QSlider::valueChanged, this, [this] { if (mriLoaded_) showMriPreviews(); });
    connect(ui_->axialSlider, &QSlider::valueChanged, this, [this] { if (mriLoaded_) showMriPreviews(); });
    const auto connectSliceStep = [this](QToolButton* button, QSlider* slider, int direction) {
        connect(button, &QToolButton::clicked, this, [slider, direction] {
            slider->setValue(slider->value() + direction);
        });
    };
    connectSliceStep(ui_->sagittalPreviousButton, ui_->sagittalSlider, -1);
    connectSliceStep(ui_->sagittalNextButton, ui_->sagittalSlider, 1);
    connectSliceStep(ui_->coronalPreviousButton, ui_->coronalSlider, -1);
    connectSliceStep(ui_->coronalNextButton, ui_->coronalSlider, 1);
    connectSliceStep(ui_->axialPreviousButton, ui_->axialSlider, -1);
    connectSliceStep(ui_->axialNextButton, ui_->axialSlider, 1);
    connectSliceStep(ui_->registrationSagittalPreviousButton, ui_->registrationSagittalSlider, -1);
    connectSliceStep(ui_->registrationSagittalNextButton, ui_->registrationSagittalSlider, 1);
    connectSliceStep(ui_->registrationCoronalPreviousButton, ui_->registrationCoronalSlider, -1);
    connectSliceStep(ui_->registrationCoronalNextButton, ui_->registrationCoronalSlider, 1);
    connectSliceStep(ui_->registrationAxialPreviousButton, ui_->registrationAxialSlider, -1);
    connectSliceStep(ui_->registrationAxialNextButton, ui_->registrationAxialSlider, 1);
    connect(ui_->registrationSagittalSlider, &QSlider::valueChanged, ui_->sagittalSlider, &QSlider::setValue);
    connect(ui_->registrationCoronalSlider, &QSlider::valueChanged, ui_->coronalSlider, &QSlider::setValue);
    connect(ui_->registrationAxialSlider, &QSlider::valueChanged, ui_->axialSlider, &QSlider::setValue);
    connect(ui_->sagittalSlider, &QSlider::valueChanged, ui_->registrationSagittalSlider, &QSlider::setValue);
    connect(ui_->coronalSlider, &QSlider::valueChanged, ui_->registrationCoronalSlider, &QSlider::setValue);
    connect(ui_->axialSlider, &QSlider::valueChanged, ui_->registrationAxialSlider, &QSlider::setValue);
    // Treatment uses its own three-plane viewer, while sharing the loaded
    // volume and slice positions with Imaging/Registration.
    for (auto pair : {std::pair<QSlider*, QSlider*>{ui_->sagittalSlider, treatmentSagittalSlider_},
                      std::pair<QSlider*, QSlider*>{ui_->coronalSlider, treatmentCoronalSlider_},
                      std::pair<QSlider*, QSlider*>{ui_->axialSlider, treatmentAxialSlider_}}) {
        connect(pair.first, &QSlider::rangeChanged, pair.second, &QSlider::setRange);
        connect(pair.first, &QSlider::valueChanged, pair.second, &QSlider::setValue);
        connect(pair.second, &QSlider::valueChanged, pair.first, &QSlider::setValue);
    }
    const auto treatmentPointPicked = [this](const Eigen::Vector3d& pointMm) {
        if (!mriLoaded_ || !treatmentTargetTable_) return;
        int row = treatmentTargetTable_->currentRow();
        if (row < 0) {
            row = 0;
            treatmentTargetTable_->selectRow(row);
        }
        targetMm_ = pointMm;
        const auto setCoordinate = [this, row](int column, double value) {
            if (auto* item = treatmentTargetTable_->item(row, column))
                item->setText(QString::number(value, 'f', 2));
        };
        setCoordinate(1, pointMm.x());
        setCoordinate(2, pointMm.y());
        setCoordinate(3, pointMm.z());
        if (auto* status = treatmentTargetTable_->item(row, 4)) status->setText(QStringLiteral("Selected"));
        treatmentPlanTargetLabel_->setText(QStringLiteral("Selected target (RAS): LR %1 mm, AP %2 mm, IS %3 mm")
                                                .arg(pointMm.x(), 0, 'f', 2).arg(pointMm.y(), 0, 'f', 2).arg(pointMm.z(), 0, 'f', 2));
        showMriPreviews();
    };
    for (WorkflowMriView* view : {treatmentSagittalPreview_, treatmentCoronalPreview_, treatmentAxialPreview_}) {
        view->setPointPickedHandler(treatmentPointPicked);
        view->setNavigationCrosshairVisible(true);
    }
    connect(treatmentTargetTable_, &QTableWidget::currentCellChanged, this,
            [this](int row, int, int, int) {
                if (row < 0 || row >= treatmentTargetTable_->rowCount()) return;
                treatmentTargetTable_->selectRow(row);
                bool valid = false;
                Eigen::Vector3d point;
                for (int axis = 0; axis < 3; ++axis) {
                    bool axisValid = false;
                    point(axis) = treatmentTargetTable_->item(row, axis + 1)->text().toDouble(&axisValid);
                    valid = valid || axisValid;
                }
                if (valid) {
                    targetMm_ = point;
                    showMriPreviews();
                }
            });
    connect(ui_->resetSagittalButton, &QToolButton::clicked, this, [this] {
        ui_->sagittalSlider->setValue(static_cast<int>((mriVolume_.nx - 1) / 2));
    });
    connect(ui_->resetCoronalButton, &QToolButton::clicked, this, [this] {
        ui_->coronalSlider->setValue(static_cast<int>((mriVolume_.ny - 1) / 2));
    });
    connect(ui_->resetAxialButton, &QToolButton::clicked, this, [this] {
        ui_->axialSlider->setValue(static_cast<int>((mriVolume_.nz - 1) / 2));
    });
    connect(ui_->registrationResetSagittalButton, &QToolButton::clicked, ui_->resetSagittalButton, &QToolButton::click);
    connect(ui_->registrationResetCoronalButton, &QToolButton::clicked, ui_->resetCoronalButton, &QToolButton::click);
    connect(ui_->registrationResetAxialButton, &QToolButton::clicked, ui_->resetAxialButton, &QToolButton::click);
    ui_->imagingFiducialLayout->setSelectionHandler([this](int markerIndex) {
        if (!mriLoaded_ || markerIndex < 0 || markerIndex >= static_cast<int>(fiducials_.size())) return;
        ui_->imagingFiducialLayout->setSelectedIndex(markerIndex);
        const Eigen::Vector3d markerMm = fiducials_[static_cast<std::size_t>(markerIndex)].position * 1000.0;
        const auto voxel = beam::gui::imagePositionToVoxelIndex(markerMm, mriAxes_);
        ui_->sagittalSlider->setValue(static_cast<int>(voxel.i));
        ui_->coronalSlider->setValue(static_cast<int>(voxel.j));
        ui_->axialSlider->setValue(static_cast<int>(voxel.k));
        showMessage(QStringLiteral("Showing fiducial %1 on all three planes.")
                        .arg(QString::fromStdString(fiducials_[static_cast<std::size_t>(markerIndex)].name)), false);
    });
    connect(ui_->resetInitialSlicesButton, &QPushButton::clicked, this, [this] {
        if (!mriLoaded_) return;
        ui_->sagittalSlider->setValue(static_cast<int>((mriVolume_.nx - 1) / 2));
        ui_->coronalSlider->setValue(static_cast<int>((mriVolume_.ny - 1) / 2));
        ui_->axialSlider->setValue(static_cast<int>((mriVolume_.nz - 1) / 2));
        ui_->imagingFiducialLayout->setSelectedIndex(-1);
        showMessage(QStringLiteral("MRI returned to the initially loaded LR, AP, and IS slices."), false);
    });
    connect(ui_->registrationResetInitialSlicesButton, &QPushButton::clicked,
            ui_->resetInitialSlicesButton, &QPushButton::click);
    connect(ui_->registrationReturnStartButton, &QPushButton::clicked, this, [this] {
        if (!mriLoaded_ || registrationStartFiducialRow_ < 0) return;
        ui_->registrationTable->setCurrentCell(registrationStartFiducialRow_, 0);
        navigateToRegistrationFiducial(registrationStartFiducialRow_);
        showMessage(QStringLiteral("Returned to the view shown when Registration was first opened."), false);
    });
    connect(ui_->acceptImagingButton, &QPushButton::clicked, this, [this] {
        if (!mriLoaded_) {
            showMessage(QStringLiteral("Load a patient MRI before accepting imaging."), true);
            return;
        }
        std::string reason;
        if (workflow_.complete(beam::gui::WorkflowStage::Imaging, &reason)) {
            showMessage(QStringLiteral("MRI accepted. Continuing to Registration."), false);
            refresh();
            updateRegistrationAvailability();
            ui_->stageList->setCurrentRow(static_cast<int>(workflow_.nextStage()));
        } else {
            showMessage(QString::fromStdString(reason), true);
            refresh();
            updateRegistrationAvailability();
        }
    });
    connect(ui_->resetRegistrationButton, &QPushButton::clicked, this, [this] {
        fiducialConfirmed_ = sourceFiducialConfirmed_;
        fiducialLocated_ = sourceFiducialLocated_;
        fiducials_ = registrationSourceFiducials_;
        populateRegistrationTable();
        showMriPreviews();
        setRegistrationPhase(RegistrationPhase::Locating);
        workflow_.change(beam::gui::WorkflowStage::Registration,
                         "Registration measurements changed; later approvals require review.");
        ui_->registrationResult->setText(QStringLiteral("Original fiducial measurements restored. Review them, then fit the transducers."));
        refresh();
        updateRegistrationAvailability();
    });
    connect(ui_->placeFiducialButton, &QPushButton::clicked, this, [this] { beginFiducialPlacement(); });
    connect(ui_->confirmFiducialButton, &QPushButton::clicked, this, [this] { confirmSelectedFiducial(); });
    connect(ui_->registerFiducialsButton, &QPushButton::clicked, this, [this] { performFiducialRegistration(); });
    connect(ui_->acceptRegistrationButton, &QPushButton::clicked, this, [this] { acceptFiducialRegistration(); });
    connect(ui_->registrationTable, &QTableWidget::currentCellChanged, this,
            [this](int currentRow, int, int, int) {
                ui_->registrationFiducialLayout->setSelectedIndex(currentRow);
                if (currentRow >= 0) ui_->registrationTable->selectRow(currentRow);
                for (int row = 0; row < ui_->registrationTable->rowCount(); ++row) {
                    for (int column = 0; column < ui_->registrationTable->columnCount(); ++column) {
                        if (auto* item = ui_->registrationTable->item(row, column))
                            item->setBackground(row == currentRow ? QColor(190, 235, 245)
                                                                   : QColor(Qt::transparent));
                    }
                }
                if (!suppressRegistrationNavigation_)
                    navigateToRegistrationFiducial(currentRow);
                if (registrationFitApplied() && registrationLockPositionRegistered())
                    setRegistrationPhase(RegistrationPhase::FitApplied);
                updateRegistrationAvailability();
                // Selecting a Step 1/2 item signals that the operator wants
                // to review or rerun the MRI-fiducial fit.
            });
    ui_->registrationTable->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui_->registrationTable, &QTableWidget::customContextMenuRequested, this,
            [this](const QPoint& position) {
                const int row = ui_->registrationTable->rowAt(position.y());
                if (row < 0 || row >= ui_->registrationTable->rowCount()) return;
                ui_->registrationTable->setCurrentCell(row, 0);
                QMenu menu(this);
                QAction* confirm = menu.addAction(QStringLiteral("Confirm this fiducial"));
                confirm->setEnabled(row >= 0 && row < 6 &&
                                    fiducialLocated_[static_cast<std::size_t>(row)] &&
                                    !fiducialConfirmed_[static_cast<std::size_t>(row)]);
                connect(confirm, &QAction::triggered, this, [this, row] {
                    ui_->registrationTable->setCurrentCell(row, 0);
                    confirmSelectedFiducial();
                });
                menu.exec(ui_->registrationTable->viewport()->mapToGlobal(position));
            });
    connect(ui_->registrationTable, &QTableWidget::cellDoubleClicked, this,
            [this](int row, int) {
                if (row < 0 || row >= 6) return;
                ui_->registrationTable->setCurrentCell(row, 0);
                confirmSelectedFiducial();
            });
    ui_->registrationFiducialLayout->setSelectionHandler([this](int markerIndex) {
        if (markerIndex < 0 || markerIndex >= 6) return;
        ui_->registrationTable->setCurrentCell(markerIndex, 0);
        // setCurrentCell() emits no signal when the same row is clicked again.
        // Navigate explicitly so a repeated triangle click still returns the
        // MRI views to that marker after Reset to Initial Slice.
        navigateToRegistrationFiducial(markerIndex);
        if (registrationFitApplied() && registrationLockPositionRegistered())
            setRegistrationPhase(RegistrationPhase::FitApplied);
    });
    connect(ui_->registrationTable, &QTableWidget::cellChanged, this, [this](int changedRow, int column) {
        if (!registrationGeometryLoaded_ || column == 0 || column >= 4) return;
        // Use the row reported by Qt. The current selection may be changing at
        // the same time; using currentRow() here incorrectly demoted a
        // previously Confirmed row to Located when another row was clicked.
        const int row = changedRow;
        if (row >= 0 && row < 6) {
            fiducialLocated_[static_cast<std::size_t>(row)] = true;
            fiducialConfirmed_[static_cast<std::size_t>(row)] = false;
        }
        if (row >= 0 && row < 6) {
            Eigen::Vector3d point;
            bool valid = true;
            for (int axis = 0; axis < 3; ++axis) {
                bool coordinateValid = false;
                point(axis) = ui_->registrationTable->item(row, axis + 1)->text().toDouble(&coordinateValid);
                valid = valid && coordinateValid;
            }
            if (valid) {
                fiducials_[static_cast<std::size_t>(row)].position = point / 1000.0;
                showMriPreviews();
            }
        }
        workflow_.change(beam::gui::WorkflowStage::Registration,
                         "Registration measurements changed; later approvals require review.");
        setRegistrationPhase(RegistrationPhase::Locating);
        ui_->registrationResult->setText(QStringLiteral("Measurements changed. Run registration again to accept them."));
        refresh();
        ui_->registrationTable->blockSignals(true);
        ui_->registrationTable->item(row, 4)->setText(QStringLiteral("Located"));
        ui_->registrationTable->blockSignals(false);
        updateRegistrationAvailability();
    });
    const auto picked = [this](const Eigen::Vector3d& point) { placeSelectedFiducial(point); };
    const auto pickedMarker = [this](int markerIndex) {
        if (markerIndex < 0 || markerIndex >= 6) return;
        suppressRegistrationNavigation_ = true;
        ui_->registrationTable->setCurrentCell(markerIndex, 0);
        ui_->registrationTable->selectRow(markerIndex);
        ui_->registrationTable->setFocus(Qt::OtherFocusReason);
        suppressRegistrationNavigation_ = false;
        for (int row = 0; row < ui_->registrationTable->rowCount(); ++row) {
            for (int column = 0; column < ui_->registrationTable->columnCount(); ++column) {
                if (auto* item = ui_->registrationTable->item(row, column))
                    item->setBackground(row == markerIndex ? QColor(190, 235, 245)
                                                            : QColor(Qt::transparent));
            }
        }
        ui_->registrationFiducialLayout->setSelectedIndex(markerIndex);
    };
    ui_->registrationSagittalPreview->setPointPickedHandler(picked);
    ui_->registrationCoronalPreview->setPointPickedHandler(picked);
    ui_->registrationAxialPreview->setPointPickedHandler(picked);
    ui_->registrationSagittalPreview->setMarkerPickedHandler(pickedMarker);
    ui_->registrationCoronalPreview->setMarkerPickedHandler(pickedMarker);
    ui_->registrationAxialPreview->setMarkerPickedHandler(pickedMarker);
    ui_->registrationTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui_->registrationTable->horizontalHeader()->setFixedHeight(30);
    // Do not present a residual/error score to the operator.  The nominal
    // array geometry is a model, not ground truth, so this value would imply
    // an accuracy that the workflow cannot independently establish.
    ui_->registrationTable->setColumnHidden(5, true);
    ui_->registrationTable->verticalHeader()->setDefaultSectionSize(25);
    ui_->registrationTable->verticalHeader()->setMinimumSectionSize(22);
    // Six complete rows plus the header, without an oversized viewport.
    ui_->registrationTable->setFixedHeight(186);
    if (auto* fiducialHeader = ui_->registrationTable->horizontalHeaderItem(0))
        fiducialHeader->setText(QStringLiteral("Fiducial"));
    ui_->acceptRegistrationButton->setText(QStringLiteral("4  Accept registration and continue →"));
    // The large translucent Step 4 badge is separate from the action text;
    // remove the duplicated numeral from the button caption.
    ui_->acceptRegistrationButton->setText(ui_->acceptRegistrationButton->text().mid(3));
    registrationStep2Label_->hide();
    registrationStep4Label_->hide();
    ui_->registrationTable->setStyleSheet(
        QStringLiteral("QTableWidget { color: #17313f; background: #ffffff; } "
                       "QTableWidget::item { color: #17313f; background: #ffffff; } "
                       "QTableWidget::item:alternate { color: #17313f; background: #f1f6f8; } "
                       "QTableWidget::item:selected, QTableWidget::item:selected:!active "
                       "{ background: #58c7e5; color: #083d4d; }"));
    ui_->registrationTable->horizontalHeader()->setStyleSheet(
        QStringLiteral("QHeaderView::section { padding: 0px; margin: 0px; }"));
    connect(ui_->participantEdit, &QLineEdit::textChanged, this, [this](const QString& value) {
        ui_->participantHeader->setText(QStringLiteral("Participant: %1").arg(value.isEmpty() ? QStringLiteral("—") : value));
    });
    ui_->visitEdit->setValidator(new QIntValidator(1, 99, ui_->visitEdit));
    const auto updateVisitPresentation = [this](const QString& text) {
        ui_->visitHeader->setText(ui_->visitEdit->hasAcceptableInput()
                                      ? QStringLiteral("Visit: %1").arg(text)
                                      : QStringLiteral("Visit: —"));
    };
    connect(ui_->visitEdit, &QLineEdit::textChanged, this, updateVisitPresentation);
    updateVisitPresentation(ui_->visitEdit->text());
    connect(ui_->abortButton, &QPushButton::clicked, this, [this] {
        showMessage(QStringLiteral("No treatment is active; no device command was sent."), true);
    });

    ui_->stageList->setCurrentRow(0);
    ui_->acceptCaseButton->setEnabled(true);
    ui_->acceptCaseButton->setFocusPolicy(Qt::StrongFocus);
    ui_->acceptCaseButton->setAutoDefault(true);
    ui_->acceptCaseButton->setDefault(true);
    ui_->acceptImagingButton->setFocusPolicy(Qt::StrongFocus);
    ui_->acceptImagingButton->setAutoDefault(true);
    ui_->acceptImagingButton->setDefault(true);
    for (QPushButton* button : {ui_->simulatePassButton, ui_->simulateFailButton,
                                ui_->acceptDeviceReadinessButton}) {
        button->setFocusPolicy(Qt::StrongFocus);
        button->setAutoDefault(true);
        button->setDefault(true);
        button->installEventFilter(this);
    }
    refresh();
    // Start the operator in the first required identification field so the
    // Participant ID can be entered immediately after Beam opens.
    QTimer::singleShot(0, this, [this] {
        ui_->participantEdit->setFocus(Qt::OtherFocusReason);
    });
}

void WorkflowWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    // Let Qt finish the new layout first.  Applying fixed preview heights
    // during the resize event can capture an intermediate zero/small height,
    // which makes the registration sliders vanish after repeated maximize /
    // restore cycles.
    QTimer::singleShot(0, this, [this] { syncRegistrationPreviewHeights(); });
}

bool WorkflowWindow::eventFilter(QObject* watched, QEvent* event) {
    if ((watched == ui_->simulatePassButton || watched == ui_->simulateFailButton ||
         watched == ui_->acceptDeviceReadinessButton) &&
        event->type() == QEvent::KeyPress) {
        const auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
            static_cast<QPushButton*>(watched)->click();
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void WorkflowWindow::syncRegistrationPreviewHeights() {
    if (!ui_ || !ui_->sagittalPreview) return;
    const int imagingHeight = ui_->sagittalPreview->height();
    if (imagingHeight < 120 || !ui_->registrationPage) return;
    // Registration has a second work area below the MRI row (fiducial table
    // plus lock-position calibration).  In a normal-sized window the Imaging
    // preview height can consume that entire area, hiding Steps 3 and 4. Keep
    // the Imaging height as the upper bound, but reserve enough room for the
    // registration controls below it.
    const int availablePageHeight = ui_->registrationPage->height();
    const int registrationPreviewHeight = std::clamp(availablePageHeight - 500, 220, imagingHeight);
    // Account for the group margins, slice slider row, and coordinate label
    // below each preview.  Keep a generous reserve for the four tool buttons
    // in the slider row; otherwise the details panel can be laid out over the
    // lower half of those buttons after a maximize/restore cycle.
    const int registrationGroupHeight = registrationPreviewHeight + 64;
    // Do not let the three-image row negotiate itself below the child layouts.
    // This is especially important when the stacked page becomes visible
    // after a window-state change.
    ui_->registrationImages->setSizeConstraint(QLayout::SetMinimumSize);
    for (QGroupBox* group : {ui_->registrationSagittalGroup,
                             ui_->registrationCoronalGroup,
                             ui_->registrationAxialGroup}) {
        group->setMinimumHeight(registrationGroupHeight);
        group->setMaximumHeight(registrationGroupHeight);
    }
    for (WorkflowMriView* view : {ui_->registrationSagittalPreview,
                                  ui_->registrationCoronalPreview,
                                  ui_->registrationAxialPreview}) {
        if (!view || view->minimumHeight() == registrationPreviewHeight &&
                      view->maximumHeight() == registrationPreviewHeight) continue;
        view->setMinimumHeight(registrationPreviewHeight);
        view->setMaximumHeight(registrationPreviewHeight);
    }
}

WorkflowWindow::~WorkflowWindow() { delete ui_; }

bool WorkflowWindow::registrationFitApplied() const {
    return registrationPhase_ == RegistrationPhase::FitApplied ||
           registrationPhase_ == RegistrationPhase::LockPositionRegistered ||
           registrationPhase_ == RegistrationPhase::Accepted;
}

bool WorkflowWindow::registrationLockPositionRegistered() const {
    return registrationPhase_ == RegistrationPhase::LockPositionRegistered ||
           registrationPhase_ == RegistrationPhase::Accepted;
}

void WorkflowWindow::setRegistrationPhase(RegistrationPhase phase) {
    registrationPhase_ = phase;
    updateRegistrationAvailability();
}

void WorkflowWindow::chooseNifti() {
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Load patient MRI"), QString(),
        QStringLiteral("NIfTI images (*.nii *.nii.gz);;All files (*)"));
    if (!path.isEmpty()) loadMri(path);
}

void WorkflowWindow::chooseDicomDirectory() {
    const QString path = QFileDialog::getExistingDirectory(this, QStringLiteral("Select DICOM series folder"));
    if (!path.isEmpty()) loadMri(path);
}

void WorkflowWindow::chooseBeamSession() {
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Load legacy Beam MRI session"), QString(),
                                                       QStringLiteral("MATLAB Beam sessions (*.mat)"));
    if (path.isEmpty()) return;
    auto* progress = new QProgressDialog(QStringLiteral("Loading MRI from Beam session…"), QString(), 0, 0, this);
    progress->setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    progress->setWindowModality(Qt::WindowModal);
    progress->setMinimumWidth(520);
    progress->setMinimumDuration(0);
    progress->setCancelButton(nullptr);
    progress->setAutoClose(false);
    progress->setAutoReset(false);
    progress->show();
    ui_->loadBeamSessionButton->setEnabled(false);

    using Import = beam::infra::mat::LegacyBeamMri;
    struct ImportState { std::optional<Import> value; std::exception_ptr error; };
    const auto state = std::make_shared<ImportState>();
    QThread* worker = QThread::create([state, path] {
        try { state->value = beam::infra::mat::loadLegacyBeamMri(path.toStdString()); }
        catch (...) { state->error = std::current_exception(); }
    });
    connect(worker, &QThread::finished, this, [this, state, progress, path] {
        progress->close();
        progress->deleteLater();
        ui_->loadBeamSessionButton->setEnabled(true);
        try {
            if (state->error) std::rethrow_exception(state->error);
            auto imported = std::move(state->value.value());
            installMri(std::move(imported.volume), std::move(imported.axes), path);
            rebuildFocusImage();
            showMriPreviews();
            if (imported.fiducials.size() == 6) {
                fiducials_.clear();
                for (std::size_t fiducialIndex = 0; fiducialIndex < imported.fiducials.size(); ++fiducialIndex) {
                    const auto& source = imported.fiducials[fiducialIndex];
                    beam::registration::FiducialMarker marker;
                    marker.name = source.name;
                    marker.position = source.positionMm / 1000.0;
                    fiducials_.push_back(marker);
                }
                registrationSourceFiducials_ = fiducials_;
                fiducialConfirmed_.fill(true);
                sourceFiducialConfirmed_.fill(true);
                fiducialLocated_.fill(true);
                sourceFiducialLocated_.fill(true);
                populateRegistrationTable();
                showMriPreviews();
                ui_->registrationResult->setText(
                    QStringLiteral("Six saved MRI fiducials imported from the Beam session · requires review"));
                showMessage(QStringLiteral("Beam MRI and six saved fiducials imported read-only. Review the MRI, then run registration."), false);
            } else {
                showMessage(QStringLiteral("Beam MRI imported; no complete six-fiducial set was found, so model estimates are shown."), true);
            }
        } catch (const std::exception& error) {
            QMessageBox::critical(this, QStringLiteral("Beam session import failed"), QString::fromUtf8(error.what()));
            showMessage(QStringLiteral("The session was not imported; the previous MRI was preserved."), true);
        }
    });
    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    worker->start();
}

void WorkflowWindow::installMri(beam::mri::Volume3D volume, beam::mri::RasAxisVectors axes,
                                const QString& path) {
    if (volume.nx < 1 || volume.ny < 1 || volume.nz < 1) throw std::runtime_error("MRI contains no image voxels");
    if (axes.dimLR.size() != volume.nx || axes.dimAP.size() != volume.ny || axes.dimIS.size() != volume.nz)
        throw std::runtime_error("MRI physical axes do not match the voxel dimensions");
    const bool replacing = mriLoaded_;
    registrationStartFiducialRow_ = -1;
    ui_->registrationReturnStartButton->setEnabled(false);
    mriVolume_ = std::move(volume);
    focusImageLoaded_ = false;
    mriAxes_ = std::move(axes);
    if (mriAxes_.dimLR.size() > 1 && mriAxes_.dimLR(0) > mriAxes_.dimLR(mriAxes_.dimLR.size() - 1)) mriAxes_.dimLR.reverseInPlace();
    if (mriAxes_.dimAP.size() > 1 && mriAxes_.dimAP(0) > mriAxes_.dimAP(mriAxes_.dimAP.size() - 1)) mriAxes_.dimAP.reverseInPlace();
    if (mriAxes_.dimIS.size() > 1 && mriAxes_.dimIS(0) > mriAxes_.dimIS(mriAxes_.dimIS.size() - 1)) mriAxes_.dimIS.reverseInPlace();
    mriLoaded_ = true;
    mriPath_ = path;
    workflow_.change(beam::gui::WorkflowStage::Imaging, "MRI changed; registration and later approvals require review.");
    ui_->mriPathLabel->setText(QStringLiteral("%1  ·  Loaded: %2")
                                   .arg(path, QDateTime::currentDateTime().toString(Qt::ISODate)));
    const auto resolution = beam::mri::computeVoxelResolution(mriAxes_);
    ui_->mriMetadataLabel->setText(
        QStringLiteral("Volume: %1 x %2 x %3 voxels; spacing: %4 x %5 x %6 mm; legacy Beam session\n"
                       "RAS bounds - LR: %7 to %8 mm; AP: %9 to %10 mm; IS: %11 to %12 mm")
            .arg(mriVolume_.nx).arg(mriVolume_.ny).arg(mriVolume_.nz)
            .arg(resolution.lr,0,'f',2).arg(resolution.ap,0,'f',2).arg(resolution.is,0,'f',2)
            .arg(mriAxes_.dimLR(0),0,'f',3).arg(mriAxes_.dimLR(mriAxes_.dimLR.size()-1),0,'f',3)
            .arg(mriAxes_.dimAP(0),0,'f',3).arg(mriAxes_.dimAP(mriAxes_.dimAP.size()-1),0,'f',3)
            .arg(mriAxes_.dimIS(0),0,'f',3).arg(mriAxes_.dimIS(mriAxes_.dimIS.size()-1),0,'f',3));
    ui_->mriMetadataLabel->setText(ui_->mriMetadataLabel->text() +
        QStringLiteral("\nInitial slices - LR: %1 mm, AP: %2 mm, IS: %3 mm")
            .arg(mriAxes_.dimLR(static_cast<Eigen::Index>((mriVolume_.nx - 1) / 2)), 0, 'f', 3)
            .arg(mriAxes_.dimAP(static_cast<Eigen::Index>((mriVolume_.ny - 1) / 2)), 0, 'f', 3)
            .arg(mriAxes_.dimIS(static_cast<Eigen::Index>((mriVolume_.nz - 1) / 2)), 0, 'f', 3));
    ui_->mriMetadataLabel->setText(ui_->mriMetadataLabel->text().left(
        ui_->mriMetadataLabel->text().indexOf(QStringLiteral("Initial slices"))) +
        QStringLiteral("\nInitial slices - LR: %1 mm, AP: %2 mm, IS: %3 mm")
            .arg(mriAxes_.dimLR(static_cast<Eigen::Index>((mriVolume_.nx - 1) / 2)), 0, 'f', 3)
            .arg(mriAxes_.dimAP(static_cast<Eigen::Index>((mriVolume_.ny - 1) / 2)), 0, 'f', 3)
            .arg(mriAxes_.dimIS(static_cast<Eigen::Index>((mriVolume_.nz - 1) / 2)), 0, 'f', 3));
    ui_->sagittalSlider->setRange(0, static_cast<int>(mriVolume_.nx - 1));
    ui_->coronalSlider->setRange(0, static_cast<int>(mriVolume_.ny - 1));
    ui_->axialSlider->setRange(0, static_cast<int>(mriVolume_.nz - 1));
    ui_->registrationSagittalSlider->setRange(0, static_cast<int>(mriVolume_.nx - 1));
    ui_->registrationCoronalSlider->setRange(0, static_cast<int>(mriVolume_.ny - 1));
    ui_->registrationAxialSlider->setRange(0, static_cast<int>(mriVolume_.nz - 1));
    treatmentSagittalSlider_->setRange(0, static_cast<int>(mriVolume_.nx - 1));
    treatmentCoronalSlider_->setRange(0, static_cast<int>(mriVolume_.ny - 1));
    treatmentAxialSlider_->setRange(0, static_cast<int>(mriVolume_.nz - 1));
    ui_->sagittalSlider->setValue(static_cast<int>((mriVolume_.nx - 1) / 2));
    ui_->coronalSlider->setValue(static_cast<int>((mriVolume_.ny - 1) / 2));
    ui_->axialSlider->setValue(static_cast<int>((mriVolume_.nz - 1) / 2));
    for (QSlider* slider : {ui_->sagittalSlider, ui_->coronalSlider, ui_->axialSlider,
                            ui_->registrationSagittalSlider, ui_->registrationCoronalSlider, ui_->registrationAxialSlider,
                            treatmentSagittalSlider_, treatmentCoronalSlider_, treatmentAxialSlider_}) slider->setEnabled(true);
    ui_->resetSagittalButton->setEnabled(true); ui_->resetCoronalButton->setEnabled(true); ui_->resetAxialButton->setEnabled(true);
    for (QToolButton* button : {ui_->sagittalPreviousButton, ui_->sagittalNextButton,
                                ui_->coronalPreviousButton, ui_->coronalNextButton,
                                ui_->axialPreviousButton, ui_->axialNextButton,
                                ui_->registrationSagittalPreviousButton, ui_->registrationSagittalNextButton,
                                ui_->registrationCoronalPreviousButton, ui_->registrationCoronalNextButton,
                                ui_->registrationAxialPreviousButton, ui_->registrationAxialNextButton,
                                ui_->registrationResetSagittalButton, ui_->registrationResetCoronalButton,
                                ui_->registrationResetAxialButton}) button->setEnabled(true);
    ui_->acceptImagingButton->setEnabled(true);
    ui_->resetInitialSlicesButton->setEnabled(true);
    ui_->registrationResetInitialSlicesButton->setEnabled(true);
    resetMriViews();
    initializeRegistrationGeometry();
    showMriPreviews();
    showMessage(replacing ? QStringLiteral("Replacement MRI loaded; review and accept it.")
                          : QStringLiteral("MRI loaded. Verify all three views, then accept it."), replacing);
    refresh();
}

void WorkflowWindow::loadMri(const QString& path) {
    try {
        const beam::mri::MriVolumeRas ras = beam::infra::dicom::loadMriRas(path.toStdString());
        beam::mri::Volume3D volume = beam::mri::reorientedVolumeToVolume3D(ras.volume);
        if (volume.nx < 1 || volume.ny < 1 || volume.nz < 1) throw std::runtime_error("MRI contains no image voxels");
        if (ras.axes.dimLR.size() != volume.nx || ras.axes.dimAP.size() != volume.ny ||
            ras.axes.dimIS.size() != volume.nz) {
            throw std::runtime_error("MRI physical axes do not match the RAS-oriented voxel dimensions");
        }

        const bool replacing = mriLoaded_;
        registrationStartFiducialRow_ = -1;
        ui_->registrationReturnStartButton->setEnabled(false);
        mriVolume_ = std::move(volume);
        focusImageLoaded_ = false;
        mriAxes_ = ras.axes;
        // applyVoxelRasXform3D makes voxel indices increase in R/A/S. Keep
        // the displayed physical axes in that same order after a source flip.
        if (mriAxes_.dimLR.size() > 1 && mriAxes_.dimLR(0) > mriAxes_.dimLR(mriAxes_.dimLR.size() - 1))
            mriAxes_.dimLR.reverseInPlace();
        if (mriAxes_.dimAP.size() > 1 && mriAxes_.dimAP(0) > mriAxes_.dimAP(mriAxes_.dimAP.size() - 1))
            mriAxes_.dimAP.reverseInPlace();
        if (mriAxes_.dimIS.size() > 1 && mriAxes_.dimIS(0) > mriAxes_.dimIS(mriAxes_.dimIS.size() - 1))
            mriAxes_.dimIS.reverseInPlace();
        mriLoaded_ = true;
        mriPath_ = path;
        workflow_.change(beam::gui::WorkflowStage::Imaging,
                         "MRI changed; registration and later approvals require review.");
        ui_->mriPathLabel->setText(QStringLiteral("%1  ·  Loaded: %2")
                                       .arg(path, QDateTime::currentDateTime().toString(Qt::ISODate)));
        const beam::mri::VoxelResolution resolution = beam::mri::computeVoxelResolution(mriAxes_);
        ui_->mriMetadataLabel->setText(
        QStringLiteral("Volume: %1 x %2 x %3 voxels; spacing: %4 x %5 x %6 mm; RAS oriented\n"
                       "RAS bounds - LR: %7 to %8 mm; AP: %9 to %10 mm; IS: %11 to %12 mm")
                .arg(mriVolume_.nx).arg(mriVolume_.ny).arg(mriVolume_.nz)
                .arg(resolution.lr, 0, 'f', 2).arg(resolution.ap, 0, 'f', 2).arg(resolution.is, 0, 'f', 2)
                .arg(mriAxes_.dimLR(0),0,'f',3).arg(mriAxes_.dimLR(mriAxes_.dimLR.size()-1),0,'f',3)
                .arg(mriAxes_.dimAP(0),0,'f',3).arg(mriAxes_.dimAP(mriAxes_.dimAP.size()-1),0,'f',3)
                .arg(mriAxes_.dimIS(0),0,'f',3).arg(mriAxes_.dimIS(mriAxes_.dimIS.size()-1),0,'f',3));
        ui_->mriMetadataLabel->setText(ui_->mriMetadataLabel->text() +
            QStringLiteral("\nInitial slices - LR: %1 mm, AP: %2 mm, IS: %3 mm")
                .arg(mriAxes_.dimLR(static_cast<Eigen::Index>((mriVolume_.nx - 1) / 2)), 0, 'f', 3)
                .arg(mriAxes_.dimAP(static_cast<Eigen::Index>((mriVolume_.ny - 1) / 2)), 0, 'f', 3)
                .arg(mriAxes_.dimIS(static_cast<Eigen::Index>((mriVolume_.nz - 1) / 2)), 0, 'f', 3));
        ui_->mriMetadataLabel->setText(ui_->mriMetadataLabel->text().left(
            ui_->mriMetadataLabel->text().indexOf(QStringLiteral("Initial slices"))) +
            QStringLiteral("\nInitial slices - LR: %1 mm, AP: %2 mm, IS: %3 mm")
                .arg(mriAxes_.dimLR(static_cast<Eigen::Index>((mriVolume_.nx - 1) / 2)), 0, 'f', 3)
                .arg(mriAxes_.dimAP(static_cast<Eigen::Index>((mriVolume_.ny - 1) / 2)), 0, 'f', 3)
                .arg(mriAxes_.dimIS(static_cast<Eigen::Index>((mriVolume_.nz - 1) / 2)), 0, 'f', 3));
        ui_->sagittalSlider->setRange(0, static_cast<int>(mriVolume_.nx - 1));
        ui_->coronalSlider->setRange(0, static_cast<int>(mriVolume_.ny - 1));
        ui_->axialSlider->setRange(0, static_cast<int>(mriVolume_.nz - 1));
        ui_->registrationSagittalSlider->setRange(0, static_cast<int>(mriVolume_.nx - 1));
        ui_->registrationCoronalSlider->setRange(0, static_cast<int>(mriVolume_.ny - 1));
        ui_->registrationAxialSlider->setRange(0, static_cast<int>(mriVolume_.nz - 1));
        treatmentSagittalSlider_->setRange(0, static_cast<int>(mriVolume_.nx - 1));
        treatmentCoronalSlider_->setRange(0, static_cast<int>(mriVolume_.ny - 1));
        treatmentAxialSlider_->setRange(0, static_cast<int>(mriVolume_.nz - 1));
        ui_->sagittalSlider->setValue(static_cast<int>((mriVolume_.nx - 1) / 2));
        ui_->coronalSlider->setValue(static_cast<int>((mriVolume_.ny - 1) / 2));
        ui_->axialSlider->setValue(static_cast<int>((mriVolume_.nz - 1) / 2));
        ui_->sagittalSlider->setEnabled(true);
        ui_->coronalSlider->setEnabled(true);
        ui_->axialSlider->setEnabled(true);
        ui_->registrationSagittalSlider->setEnabled(true);
        ui_->registrationCoronalSlider->setEnabled(true);
        ui_->registrationAxialSlider->setEnabled(true);
        treatmentSagittalSlider_->setEnabled(true);
        treatmentCoronalSlider_->setEnabled(true);
        treatmentAxialSlider_->setEnabled(true);
        ui_->resetSagittalButton->setEnabled(true);
        ui_->resetCoronalButton->setEnabled(true);
        ui_->resetAxialButton->setEnabled(true);
        for (QToolButton* button : {ui_->sagittalPreviousButton, ui_->sagittalNextButton,
                                    ui_->coronalPreviousButton, ui_->coronalNextButton,
                                    ui_->axialPreviousButton, ui_->axialNextButton,
                                    ui_->registrationSagittalPreviousButton, ui_->registrationSagittalNextButton,
                                    ui_->registrationCoronalPreviousButton, ui_->registrationCoronalNextButton,
                                    ui_->registrationAxialPreviousButton, ui_->registrationAxialNextButton,
                                    ui_->registrationResetSagittalButton, ui_->registrationResetCoronalButton,
                                    ui_->registrationResetAxialButton}) button->setEnabled(true);
        ui_->acceptImagingButton->setEnabled(true);
        ui_->resetInitialSlicesButton->setEnabled(true);
        ui_->registrationResetInitialSlicesButton->setEnabled(true);
        resetMriViews();
        initializeRegistrationGeometry();
        showMriPreviews();
        showMessage(replacing ? QStringLiteral("Replacement MRI loaded. Downstream approvals were invalidated; review and accept it.")
                              : QStringLiteral("MRI loaded. Verify all three views, then accept the patient MRI."),
                    replacing);
        refresh();
    } catch (const std::exception& error) {
        QMessageBox::critical(this, QStringLiteral("MRI load failed"), QString::fromUtf8(error.what()));
        showMessage(QStringLiteral("MRI could not be loaded. The previous imaging state was not changed."), true);
    }
}

void WorkflowWindow::resetMriViews() {
    setPlacementMode(false);
    for (WorkflowMriView* view : {ui_->sagittalPreview, ui_->coronalPreview, ui_->axialPreview,
                                  ui_->registrationSagittalPreview, ui_->registrationCoronalPreview,
                                  ui_->registrationAxialPreview, treatmentSagittalPreview_,
                                  treatmentCoronalPreview_, treatmentAxialPreview_})
        view->resetView();
}

void WorkflowWindow::rebuildFocusImage() {
    // BeamV0/drawFocusOnMRI.m calls setFiducialTemplate at the current
    // array centre on every draw. Its identity-rotation focus template is
    // the ellipsoid (LR/30)^2 + (AP/5)^2 + (IS/5)^2 < 1, in millimetres.
    focusImage_.nx = mriVolume_.nx;
    focusImage_.ny = mriVolume_.ny;
    focusImage_.nz = mriVolume_.nz;
    focusImage_.kSlices.assign(static_cast<std::size_t>(mriVolume_.nz),
                               Eigen::MatrixXd::Zero(mriVolume_.nx, mriVolume_.ny));
    for (Eigen::Index k = 0; k < mriVolume_.nz; ++k) {
        const double dz = (mriAxes_.dimIS(k) - targetMm_.z()) / 5.0;
        if (std::abs(dz) >= 1.0) continue;
        for (Eigen::Index j = 0; j < mriVolume_.ny; ++j) {
            const double dy = (mriAxes_.dimAP(j) - targetMm_.y()) / 5.0;
            if (dy * dy + dz * dz >= 1.0) continue;
            for (Eigen::Index i = 0; i < mriVolume_.nx; ++i) {
                const double dx = (mriAxes_.dimLR(i) - targetMm_.x()) / 30.0;
                if (dx * dx + dy * dy + dz * dz < 1.0)
                    focusImage_.kSlices[static_cast<std::size_t>(k)](i, j) = 1.0;
            }
        }
    }
    focusImageLoaded_ = true;
}

void WorkflowWindow::initializeRegistrationGeometry() {
    const QString executableDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        executableDir + QStringLiteral("/../../../../DefaultSubjectV0/defaultSubjectArrayRect.csv"),
        QDir::current().filePath(QStringLiteral("../DefaultSubjectV0/defaultSubjectArrayRect.csv"))};
    QString geometryPath;
    for (const QString& candidate : candidates) {
        if (QFileInfo::exists(candidate)) { geometryPath = candidate; break; }
    }
    if (geometryPath.isEmpty()) throw std::runtime_error("Default transducer geometry was not found");

    beam::array::ArrayData nominal = beam::array::defineArrayData(readArrayRectCsv(geometryPath));
    reconstructPhysicalArrayHalves(nominal);
    const Eigen::Vector3d rectCenter = nominal.arrayTotal.rect.block(16, 0, 3, nominal.arrayTotal.rect.cols()).rowwise().mean();
    // BeamV0/initTransducers.m: centerArray = mean(rect center) - [0,50,-25] mm.
    const Eigen::Vector3d placementReference = rectCenter - Eigen::Vector3d(0.0, 0.050, -0.025);
    const Eigen::Vector3d mriCenterMeters(mriAxes_.dimLR.mean() / 1000.0,
                                          mriAxes_.dimAP.mean() / 1000.0,
                                          mriAxes_.dimIS.mean() / 1000.0);
    Eigen::Matrix4d translation = Eigen::Matrix4d::Identity();
    translation.block<3, 1>(0, 3) = mriCenterMeters - placementReference;
    beam::registration::AffineArrayResult placed = beam::registration::applyAffineToArrayData(translation, nominal);
    arrayData_ = std::move(placed.arrayData);
    fiducials_ = std::move(placed.fiducialMarkers);
    registrationSourceFiducials_ = fiducials_;
    fiducialConfirmed_.fill(false);
    sourceFiducialConfirmed_.fill(false);
    fiducialLocated_.fill(false);
    sourceFiducialLocated_.fill(false);
    registrationOriginArrayData_ = arrayData_;
    setRegistrationPhase(RegistrationPhase::Locating);
    ui_->imagingFiducialLayout->setEnabled(!fiducials_.empty());
    ui_->imagingFiducialLayout->setSelectedIndex(-1);
    targetMm_ = arrayData_.arrayTotal.rect.block(16, 0, 3, arrayData_.arrayTotal.rect.cols()).rowwise().mean() * 1000.0;
    // MATLAB drawMrImages always calls drawFocusOnMRI after its initial
    // transducer placement. Do the same for every MRI source, including raw
    // DICOM/NIfTI; previously this was only rebuilt by the MAT-session path.
    rebuildFocusImage();
    arrayMask_ = beam::gui::rasterizeArrayOntoMriGrid(arrayData_.arrayTotal, mriAxes_, mriVolume_.nx,
                                                       mriVolume_.ny, mriVolume_.nz);
    registrationGeometryLoaded_ = true;
    populateRegistrationTable();
    ui_->placeFiducialButton->setEnabled(true);
    ui_->resetRegistrationButton->setEnabled(true);
    ui_->registrationResult->setText(QStringLiteral("Ready: review all six MRI fiducial coordinates."));
}

void WorkflowWindow::populateRegistrationTable() {
    if (!registrationGeometryLoaded_ && fiducials_.empty()) return;
    ui_->registrationTable->blockSignals(true);
    for (int row = 0; row < static_cast<int>(registrationSourceFiducials_.size()) && row < 6; ++row) {
        const auto& marker = registrationSourceFiducials_[static_cast<std::size_t>(row)];
        auto* name = new QTableWidgetItem(QString::fromStdString(marker.name));
        name->setFlags(name->flags() & ~Qt::ItemIsEditable);
        ui_->registrationTable->setItem(row, 0, name);
        for (int axis = 0; axis < 3; ++axis) {
            auto* coordinate = new QTableWidgetItem(QString::number(marker.position(axis) * 1000.0, 'f', 2));
            coordinate->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            ui_->registrationTable->setItem(row, axis + 1, coordinate);
        }
        const std::size_t statusIndex = static_cast<std::size_t>(row);
        QString stateText;
        if (!fiducialLocated_[statusIndex]) stateText = QStringLiteral("Unidentified");
        else if (!fiducialConfirmed_[statusIndex]) stateText = QStringLiteral("Located");
        else if (sourceFiducialConfirmed_[statusIndex]) stateText = QStringLiteral("Imported");
        else stateText = QStringLiteral("Confirmed");
        auto* source = new QTableWidgetItem(stateText);
        source->setFlags(source->flags() & ~Qt::ItemIsEditable);
        ui_->registrationTable->setItem(row, 4, source);
        auto* residual = new QTableWidgetItem(QStringLiteral("—"));
        residual->setFlags(residual->flags() & ~Qt::ItemIsEditable);
        residual->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        ui_->registrationTable->setItem(row, 5, residual);
    }
    ui_->registrationTable->blockSignals(false);
    if (ui_->registrationTable->currentRow() < 0 && !registrationSourceFiducials_.empty())
        ui_->registrationTable->selectRow(0);
    const int selectedRow = ui_->registrationTable->currentRow();
    bool hasPendingLocated = false;
    for (std::size_t index = 0; index < fiducialLocated_.size(); ++index)
        hasPendingLocated = hasPendingLocated || (fiducialLocated_[index] && !fiducialConfirmed_[index]);
    updateRegistrationAvailability();
}

void WorkflowWindow::navigateToRegistrationFiducial(int row) {
    // Table population selects its first row. That must not move the shared
    // MRI sliders while the operator is still reviewing a newly loaded image.
    // BeamV0 only navigates after a registration fiducial control is chosen.
    if (selectedStage_ != beam::gui::WorkflowStage::Registration || !mriLoaded_ ||
        row < 0 || row >= static_cast<int>(fiducials_.size())) return;
    const Eigen::Vector3d markerMm = fiducials_[static_cast<std::size_t>(row)].position * 1000.0;
    const auto voxel = beam::gui::imagePositionToVoxelIndex(markerMm, mriAxes_);
    ui_->sagittalSlider->setValue(static_cast<int>(voxel.i));
    ui_->coronalSlider->setValue(static_cast<int>(voxel.j));
    ui_->axialSlider->setValue(static_cast<int>(voxel.k));
    showMriPreviews();
    ui_->registrationSagittalPreview->focusOn(
        QPointF(1.0 - normalizedAxisPosition(mriAxes_.dimAP, markerMm.y()),
                1.0 - normalizedAxisPosition(mriAxes_.dimIS, markerMm.z())));
    ui_->registrationCoronalPreview->focusOn(
        QPointF(normalizedAxisPosition(mriAxes_.dimLR, markerMm.x()),
                1.0 - normalizedAxisPosition(mriAxes_.dimIS, markerMm.z())));
    ui_->registrationAxialPreview->focusOn(
        QPointF(normalizedAxisPosition(mriAxes_.dimLR, markerMm.x()),
                1.0 - normalizedAxisPosition(mriAxes_.dimAP, markerMm.y())));
    showMessage(QStringLiteral("Showing %1: center its red marker on the corresponding MRI donut.")
                    .arg(QString::fromStdString(fiducials_[static_cast<std::size_t>(row)].name)), false);
}

void WorkflowWindow::setPlacementMode(bool enabled) {
    placingFiducial_ = enabled;
    ui_->registrationSagittalPreview->setPointPlacementEnabled(enabled);
    ui_->registrationCoronalPreview->setPointPlacementEnabled(enabled);
    ui_->registrationAxialPreview->setPointPlacementEnabled(enabled);
    ui_->placeFiducialButton->setText(enabled ? QStringLiteral("Cancel placement")
                                               : QStringLiteral("Place selected fiducial…"));
}

void WorkflowWindow::beginFiducialPlacement() {
    if (placingFiducial_) { setPlacementMode(false); return; }
    const int row = ui_->registrationTable->currentRow();
    if (row < 0 || row >= 6) {
        showMessage(QStringLiteral("Select one fiducial row before entering placement mode."), true);
        return;
    }
    setPlacementMode(true);
    showMessage(QStringLiteral("Click the center of %1 in any MRI plane. The current slice supplies the third coordinate.")
                    .arg(ui_->registrationTable->item(row, 0)->text()), false);
}

void WorkflowWindow::placeSelectedFiducial(const Eigen::Vector3d& positionMm) {
    const int row = ui_->registrationTable->currentRow();
    if (selectedStage_ != beam::gui::WorkflowStage::Registration || row < 0 || row >= 6) return;
    ui_->registrationTable->blockSignals(true);
    for (int axis = 0; axis < 3; ++axis)
        ui_->registrationTable->item(row, axis + 1)->setText(QString::number(positionMm(axis), 'f', 2));
    ui_->registrationTable->blockSignals(false);
    fiducialLocated_[static_cast<std::size_t>(row)] = true;
    fiducialConfirmed_[static_cast<std::size_t>(row)] = false;
    fiducials_[static_cast<std::size_t>(row)].position = positionMm / 1000.0;
    setRegistrationPhase(RegistrationPhase::Locating);
    if (placingFiducial_) setPlacementMode(false);
    ui_->registrationTable->item(row, 4)->setText(QStringLiteral("Located"));
    showMriPreviews();
    workflow_.change(beam::gui::WorkflowStage::Registration,
                     "Fiducial measurements changed; later approvals require review.");
    showMessage(QStringLiteral("Fiducial located. Verify it in another plane, then confirm the selected fiducial."), false);
    refresh();
    updateRegistrationAvailability();
}

void WorkflowWindow::confirmSelectedFiducial() {
    int confirmedCount = 0;
    for (int row = 0; row < 6; ++row) {
        const std::size_t index = static_cast<std::size_t>(row);
        if (!fiducialLocated_[index] || fiducialConfirmed_[index]) continue;
        fiducialConfirmed_[index] = true;
        if (ui_->registrationTable->item(row, 4))
            ui_->registrationTable->item(row, 4)->setText(QStringLiteral("Confirmed"));
        ++confirmedCount;
    }
    if (confirmedCount == 0) {
        showMessage(QStringLiteral("Locate at least one fiducial before confirming."), true);
        return;
    }
    setRegistrationPhase(RegistrationPhase::Locating);
    workflow_.change(beam::gui::WorkflowStage::Registration,
                     "Fiducial confirmation changed; registration must be recalculated.");
    updateRegistrationAvailability();

    showMessage(confirmedCount == 1
                    ? QStringLiteral("Fiducial confirmed.")
                    : QStringLiteral("%1 located fiducials confirmed.").arg(confirmedCount), false);
    refresh();
    QTimer::singleShot(0, this, [this] { syncRegistrationPreviewHeights(); });
}

void WorkflowWindow::updateRegistrationAvailability() {
    const bool allLocated = std::all_of(fiducialLocated_.begin(), fiducialLocated_.end(), [](bool v) { return v; });
    const bool hasPendingLocated = std::any_of(fiducialLocated_.begin(), fiducialLocated_.end(),
                                               [this, index = std::size_t{0}](bool located) mutable {
                                                   const bool pending = located && !fiducialConfirmed_[index];
                                                   ++index;
                                                   return pending;
                                               });
    const bool allMeasured = std::all_of(fiducialConfirmed_.begin(), fiducialConfirmed_.end(), [](bool v) { return v; });
    const bool imagingAccepted = workflow_.state(beam::gui::WorkflowStage::Imaging).status == beam::gui::WorkflowStatus::Complete;
    const bool geometryReady = registrationGeometryLoaded_ && imagingAccepted;
    const bool fitApplied = registrationFitApplied();
    const bool calibrationReady = geometryReady && fitApplied;

    // This is the single source of truth for Registration interaction state.
    // Handlers update model flags, then call this function; they do not manage
    // button/slider enablement independently.
    ui_->confirmFiducialButton->setEnabled(allLocated && hasPendingLocated);
    ui_->registerFiducialsButton->setEnabled(geometryReady && allMeasured && !fitApplied);
    if (registerCurrentPositionButton_)
        registerCurrentPositionButton_->setEnabled(calibrationReady && !registrationLockPositionRegistered());
    ui_->acceptRegistrationButton->setEnabled(registrationPhase_ == RegistrationPhase::LockPositionRegistered);
    for (QSlider* slider : {leftHorizontalPositionSlider_, leftVerticalPositionSlider_,
                            rightHorizontalPositionSlider_, rightVerticalPositionSlider_}) {
        if (slider) slider->setEnabled(calibrationReady);
    }
    updateRegistrationStepIndicators();
}

void WorkflowWindow::updateRegistrationStepIndicators() {
    const auto apply = [this](QLabel* label, bool active, const QString& activeColor) {
        if (!label) return;
        label->setStyleSheet(QStringLiteral(
            "QLabel { color: %1; background: rgba(18, 43, 53, 220); border-radius: 3px; "
            "font-size: %2px; font-weight: 700; }")
                                 .arg(active ? activeColor : QStringLiteral("rgba(230, 240, 244, 150)"))
                                 .arg(label == registrationStep2Label_ ? 20 : 30));
    };
    const bool registrationStarted = registrationGeometryLoaded_;
    apply(registrationStep1Label_, registrationStarted, QStringLiteral("#58d9f2"));
    apply(registrationStep2Label_, registrationStarted, QStringLiteral("#ffd166"));
    apply(registrationStep3Label_, registrationFitApplied(), QStringLiteral("#72e6a1"));
    // Step 4 becomes available as soon as the lock-position registration has
    // completed; accepting the overall Registration stage is a separate
    // action that follows this step.
    apply(registrationStep4Label_, registrationLockPositionRegistered(), QStringLiteral("#d6a5ff"));
}

void WorkflowWindow::applyRegistrationResult(beam::registration::AffineArrayResult result) {
    arrayData_ = std::move(result.arrayData);
    // Keep the red MRI fiducials at the operator-measured coordinates. The
    // registration result's marker positions are the fitted model points and
    // may differ by the residual error; BeamV0 moves the array geometry, not
    // the MRI observations.
    targetMm_ = arrayData_.arrayTotal.rect.block(16, 0, 3, arrayData_.arrayTotal.rect.cols()).rowwise().mean() * 1000.0;
    arrayMask_ = beam::gui::rasterizeArrayOntoMriGrid(arrayData_.arrayTotal, mriAxes_, mriVolume_.nx,
                                                       mriVolume_.ny, mriVolume_.nz);
    if (focusImageLoaded_) rebuildFocusImage();
    showMriPreviews();
}

void WorkflowWindow::performFiducialRegistration() {
    if (!mriLoaded_ || workflow_.state(beam::gui::WorkflowStage::Imaging).status != beam::gui::WorkflowStatus::Complete) {
        showMessage(QStringLiteral("Accept the patient MRI before registration."), true);
        return;
    }
    std::vector<Eigen::Vector3d> measured;
    measured.reserve(6);
    for (int row = 0; row < 6; ++row) {
        Eigen::Vector3d point;
        for (int axis = 0; axis < 3; ++axis) {
            const QTableWidgetItem* item = ui_->registrationTable->item(row, axis + 1);
            bool valid = false;
            const double value = item ? item->text().toDouble(&valid) : 0.0;
            if (!valid || !std::isfinite(value)) {
                showMessage(QStringLiteral("Fiducial row %1 contains an invalid coordinate.").arg(row + 1), true);
                return;
            }
            point(axis) = value;
        }
        measured.push_back(point);
    }
    try {
        auto result = beam::registration::registerArrayToFiducials(registrationOriginArrayData_, measured);
        applyRegistrationResult(std::move(result));
        setRegistrationPhase(RegistrationPhase::FitApplied);
        updateRegistrationStepIndicators();
        // Step 4 remains locked until the Step 3 lock-position registration
        // has been completed.
        ui_->registrationResult->setText(QStringLiteral(
            "Registration applied. Verify all overlays against the MRI before accepting."));
        // Registration can be initiated immediately after returning to the
        // initial slices. Bring the shared MRI views back to the selected
        // measured fiducial so the operator can review the new placement.
        int reviewRow = ui_->registrationTable->currentRow();
        if (reviewRow < 0 || reviewRow >= static_cast<int>(fiducials_.size())) reviewRow = 0;
        ui_->registrationTable->setCurrentCell(reviewRow, 0);
        navigateToRegistrationFiducial(reviewRow);
        showMessage(QStringLiteral(
            "Registration applied. Verify all six fiducials and overlays against the MRI before accepting."), false);
        refresh();
    } catch (const std::exception& error) {
        ui_->registrationResult->setText(QStringLiteral("Registration failed: %1").arg(QString::fromUtf8(error.what())));
        showMessage(QStringLiteral("Registration failed. Verify that all six points are distinct and correctly paired."), true);
    }
}

void WorkflowWindow::acceptFiducialRegistration() {
    if (registrationPhase_ != RegistrationPhase::LockPositionRegistered) {
        const QString message = QStringLiteral("Complete the Step 3 transducer lock-position registration before accepting.");
        ui_->registrationResult->setText(message);
        showMessage(message, true);
        return;
    }
    std::string reason;
    if (!workflow_.complete(beam::gui::WorkflowStage::Registration, &reason)) {
        const QString message = QString::fromStdString(reason);
        ui_->registrationResult->setText(message);
        showMessage(message, true);
        return;
    }
    setRegistrationPhase(RegistrationPhase::Accepted);
    updateRegistrationStepIndicators();
    ui_->registrationResult->setText(ui_->registrationResult->text() + QStringLiteral("; accepted"));
    showMessage(QStringLiteral("Registration accepted. Continuing to the next workflow stage."), false);
    refresh();
    // Correction is an internal half of the combined Coupling/Correction
    // gate and is intentionally hidden from the navigation list.
    const auto next = workflow_.nextStage();
    ui_->stageList->setCurrentRow(static_cast<int>(
        next == beam::gui::WorkflowStage::Correction ? beam::gui::WorkflowStage::Coupling : next));
}

void WorkflowWindow::performCurrentPositionRegistration() {
    if (!registrationFitApplied() || fiducials_.size() != 6) {
        const QString message = QStringLiteral("Run Register to MRI fiducials before calibrating the current array position.");
        ui_->registrationResult->setText(message);
        showMessage(message, true);
        return;
    }
    if (leftHorizontalPositionSlider_->value() != rightHorizontalPositionSlider_->value() ||
        leftVerticalPositionSlider_->value() != rightVerticalPositionSlider_->value()) {
        const QString message = QStringLiteral("Subject Left and Subject Right lock-position sliders must match before Step 3 can be registered.");
        ui_->registrationResult->setText(message);
        showMessage(message, true);
        return;
    }
    std::vector<Eigen::Vector3d> measured;
    measured.reserve(6);
    for (const auto& marker : fiducials_) measured.push_back(marker.position * 1000.0);
    try {
        auto result = beam::registration::registerCurrentTransducerPosition(
            registrationOriginArrayData_, measured,
            leftHorizontalPositionSlider_->value(), leftVerticalPositionSlider_->value());
        applyRegistrationResult(std::move(result));
        setRegistrationPhase(RegistrationPhase::LockPositionRegistered);
        updateRegistrationStepIndicators();
        ui_->acceptRegistrationButton->setFocus(Qt::OtherFocusReason);
        ui_->registrationResult->setText(QStringLiteral("Array registered to the current lock position. Step 3 complete; review and accept Registration."));
        showMessage(QStringLiteral("Array registered to current lock position."), false);
        refresh();
    } catch (const std::exception& error) {
        const QString message = QStringLiteral("Current-position registration failed: %1")
                                    .arg(QString::fromUtf8(error.what()));
        ui_->registrationResult->setText(message);
        showMessage(message, true);
    }
}

void WorkflowWindow::showMriPreviews() {
    QStringList markerNames;
    for (std::size_t index = 0; index < 6; ++index) {
        if (index < fiducials_.size() && !fiducials_[index].name.empty())
            markerNames.push_back(QStringLiteral("%1. %2").arg(index + 1)
                                  .arg(QString::fromStdString(fiducials_[index].name)));
        else
            markerNames.push_back(QStringLiteral("Marker %1").arg(index + 1));
    }
    for (WorkflowMriView* view : {ui_->registrationSagittalPreview,
                                  ui_->registrationCoronalPreview,
                                  ui_->registrationAxialPreview})
        view->setCoordinatePasteOptions(markerNames);
    if (fiducials_.size() >= 6) {
        std::array<QString, 6> coordinateLabels;
        for (std::size_t index = 0; index < 6; ++index) {
            const Eigen::Vector3d rasMm = fiducials_[index].position * 1000.0;
            coordinateLabels[index] = QStringLiteral("(%1, %2, %3)")
                .arg(rasMm.x(), 0, 'f', 1).arg(rasMm.y(), 0, 'f', 1).arg(rasMm.z(), 0, 'f', 1);
        }
        ui_->imagingFiducialLayout->setMarkerCoordinateLabels(coordinateLabels);
        ui_->registrationFiducialLayout->setMarkerCoordinateLabels(coordinateLabels);
    }
    const int sagittal = ui_->sagittalSlider->value() + 1;
    const int coronal = ui_->coronalSlider->value() + 1;
    const int axial = ui_->axialSlider->value() + 1;
    // BeamV0/drawMrImages.m applies XDir="reverse" to the sagittal axes.
    ui_->sagittalPreview->setSlice(beam::mri::getSliceImage(mriVolume_, sagittal, "sagital"), true);
    ui_->coronalPreview->setSlice(beam::mri::getSliceImage(mriVolume_, coronal, "coronal"));
    ui_->axialPreview->setSlice(beam::mri::getSliceImage(mriVolume_, axial, "axial"));
    ui_->registrationSagittalPreview->setSlice(beam::mri::getSliceImage(mriVolume_, sagittal, "sagital"), true);
    ui_->registrationCoronalPreview->setSlice(beam::mri::getSliceImage(mriVolume_, coronal, "coronal"));
    ui_->registrationAxialPreview->setSlice(beam::mri::getSliceImage(mriVolume_, axial, "axial"));
    treatmentSagittalPreview_->setSlice(beam::mri::getSliceImage(mriVolume_, sagittal, "sagital"), true);
    treatmentCoronalPreview_->setSlice(beam::mri::getSliceImage(mriVolume_, coronal, "coronal"));
    treatmentAxialPreview_->setSlice(beam::mri::getSliceImage(mriVolume_, axial, "axial"));
    if (focusImageLoaded_ && ui_->showFieldCheckBox->isChecked()) {
        treatmentSagittalPreview_->setMaskOverlay(beam::mri::getSliceImage(focusImage_, sagittal, "sagital"), QColor(255, 128, 128), 0.75, true);
        treatmentCoronalPreview_->setMaskOverlay(beam::mri::getSliceImage(focusImage_, coronal, "coronal"), QColor(255, 128, 128), 0.75);
        treatmentAxialPreview_->setMaskOverlay(beam::mri::getSliceImage(focusImage_, axial, "axial"), QColor(255, 128, 128), 0.75);
    } else {
        treatmentSagittalPreview_->setMaskOverlay(Eigen::MatrixXd{}, QColor(), 0.0);
        treatmentCoronalPreview_->setMaskOverlay(Eigen::MatrixXd{}, QColor(), 0.0);
        treatmentAxialPreview_->setMaskOverlay(Eigen::MatrixXd{}, QColor(), 0.0);
    }
    const double treatmentOpacity = static_cast<double>(ui_->transducerTransparencySlider->value()) / 100.0;
    if (registrationGeometryLoaded_ && ui_->showTransducersCheckBox->isChecked()) {
        treatmentSagittalPreview_->setSecondaryMaskOverlay(beam::mri::getSliceImage(arrayMask_, sagittal, "sagital"), QColor(255, 220, 40), treatmentOpacity, true);
        treatmentCoronalPreview_->setSecondaryMaskOverlay(beam::mri::getSliceImage(arrayMask_, coronal, "coronal"), QColor(255, 220, 40), treatmentOpacity);
        treatmentAxialPreview_->setSecondaryMaskOverlay(beam::mri::getSliceImage(arrayMask_, axial, "axial"), QColor(255, 220, 40), treatmentOpacity);
    } else {
        treatmentSagittalPreview_->setSecondaryMaskOverlay(Eigen::MatrixXd{}, QColor(), 0.0);
        treatmentCoronalPreview_->setSecondaryMaskOverlay(Eigen::MatrixXd{}, QColor(), 0.0);
        treatmentAxialPreview_->setSecondaryMaskOverlay(Eigen::MatrixXd{}, QColor(), 0.0);
    }
    const double iNorm = mriVolume_.nx > 1 ? static_cast<double>(sagittal - 1) / (mriVolume_.nx - 1) : 0.5;
    const double jNorm = mriVolume_.ny > 1 ? static_cast<double>(coronal - 1) / (mriVolume_.ny - 1) : 0.5;
    const double kNorm = mriVolume_.nz > 1 ? static_cast<double>(axial - 1) / (mriVolume_.nz - 1) : 0.5;
    ui_->sagittalPreview->setNavigationCrosshair(QPointF(1.0 - jNorm, 1.0 - kNorm));
    ui_->coronalPreview->setNavigationCrosshair(QPointF(iNorm, 1.0 - kNorm));
    ui_->axialPreview->setNavigationCrosshair(QPointF(iNorm, 1.0 - jNorm));
    ui_->registrationSagittalPreview->setNavigationCrosshair(QPointF(1.0 - jNorm, 1.0 - kNorm));
    ui_->registrationCoronalPreview->setNavigationCrosshair(QPointF(iNorm, 1.0 - kNorm));
    ui_->registrationAxialPreview->setNavigationCrosshair(QPointF(iNorm, 1.0 - jNorm));
    treatmentSagittalPreview_->setNavigationCrosshair(QPointF(1.0 - jNorm, 1.0 - kNorm));
    treatmentCoronalPreview_->setNavigationCrosshair(QPointF(iNorm, 1.0 - kNorm));
    treatmentAxialPreview_->setNavigationCrosshair(QPointF(iNorm, 1.0 - jNorm));

    // Imaging is the unregistered source image review. Registration geometry is
    // deliberately confined to the Registration page so model estimates cannot
    // be mistaken for fiducials already identified in the patient's MRI.
    if (focusImageLoaded_ && ui_->showFieldCheckBox->isChecked()) {
        ui_->sagittalPreview->setMaskOverlay(beam::mri::getSliceImage(focusImage_, sagittal, "sagital"), QColor(255, 128, 128), 0.75, true);
        ui_->coronalPreview->setMaskOverlay(beam::mri::getSliceImage(focusImage_, coronal, "coronal"), QColor(255, 128, 128), 0.75);
        ui_->axialPreview->setMaskOverlay(beam::mri::getSliceImage(focusImage_, axial, "axial"), QColor(255, 128, 128), 0.75);
    } else {
        ui_->sagittalPreview->setMaskOverlay(Eigen::MatrixXd{}, QColor(), 0.0);
        ui_->coronalPreview->setMaskOverlay(Eigen::MatrixXd{}, QColor(), 0.0);
        ui_->axialPreview->setMaskOverlay(Eigen::MatrixXd{}, QColor(), 0.0);
    }
    ui_->sagittalPreview->setMarkers({});
    ui_->coronalPreview->setMarkers({});
    ui_->axialPreview->setMarkers({});

    const double transducerOpacity = static_cast<double>(ui_->transducerTransparencySlider->value()) / 100.0;
    if (registrationGeometryLoaded_ && ui_->showTransducersCheckBox->isChecked()) {
        ui_->sagittalPreview->setSecondaryMaskOverlay(beam::mri::getSliceImage(arrayMask_, sagittal, "sagital"), QColor(255, 220, 40), transducerOpacity, true);
        ui_->coronalPreview->setSecondaryMaskOverlay(beam::mri::getSliceImage(arrayMask_, coronal, "coronal"), QColor(255, 220, 40), transducerOpacity);
        ui_->axialPreview->setSecondaryMaskOverlay(beam::mri::getSliceImage(arrayMask_, axial, "axial"), QColor(255, 220, 40), transducerOpacity);
    } else {
        ui_->sagittalPreview->setSecondaryMaskOverlay(Eigen::MatrixXd{}, QColor(), 0.0);
        ui_->coronalPreview->setSecondaryMaskOverlay(Eigen::MatrixXd{}, QColor(), 0.0);
        ui_->axialPreview->setSecondaryMaskOverlay(Eigen::MatrixXd{}, QColor(), 0.0);
    }

    if (registrationGeometryLoaded_) {
        if (focusImageLoaded_ && ui_->showFieldCheckBox->isChecked()) {
            ui_->registrationSagittalPreview->setMaskOverlay(beam::mri::getSliceImage(focusImage_, sagittal, "sagital"), QColor(255, 128, 128), 0.75, true);
            ui_->registrationCoronalPreview->setMaskOverlay(beam::mri::getSliceImage(focusImage_, coronal, "coronal"), QColor(255, 128, 128), 0.75);
            ui_->registrationAxialPreview->setMaskOverlay(beam::mri::getSliceImage(focusImage_, axial, "axial"), QColor(255, 128, 128), 0.75);
        } else {
            ui_->registrationSagittalPreview->setMaskOverlay(Eigen::MatrixXd{}, QColor(), 0.0);
            ui_->registrationCoronalPreview->setMaskOverlay(Eigen::MatrixXd{}, QColor(), 0.0);
            ui_->registrationAxialPreview->setMaskOverlay(Eigen::MatrixXd{}, QColor(), 0.0);
        }
        if (ui_->showTransducersCheckBox->isChecked()) {
            ui_->registrationSagittalPreview->setSecondaryMaskOverlay(beam::mri::getSliceImage(arrayMask_, sagittal, "sagital"), QColor(255, 220, 40), transducerOpacity, true);
            ui_->registrationCoronalPreview->setSecondaryMaskOverlay(beam::mri::getSliceImage(arrayMask_, coronal, "coronal"), QColor(255, 220, 40), transducerOpacity);
            ui_->registrationAxialPreview->setSecondaryMaskOverlay(beam::mri::getSliceImage(arrayMask_, axial, "axial"), QColor(255, 220, 40), transducerOpacity);
        } else {
            ui_->registrationSagittalPreview->setSecondaryMaskOverlay(Eigen::MatrixXd{}, QColor(), 0.0);
            ui_->registrationCoronalPreview->setSecondaryMaskOverlay(Eigen::MatrixXd{}, QColor(), 0.0);
            ui_->registrationAxialPreview->setSecondaryMaskOverlay(Eigen::MatrixXd{}, QColor(), 0.0);
        }

        std::vector<WorkflowMriMarker> sagMarkers, corMarkers, axialMarkers;
        std::vector<WorkflowMriMarker> imagingSagMarkers, imagingCorMarkers, imagingAxialMarkers;
        for (std::size_t markerIndex = 0; markerIndex < fiducials_.size(); ++markerIndex) {
            const auto& marker = fiducials_[markerIndex];
            const QString markerLabel = QStringLiteral("%1. %2").arg(markerIndex + 1)
                .arg(QString::fromStdString(marker.name));
            const bool draggable = selectedStage_ == beam::gui::WorkflowStage::Registration;
            const Eigen::Vector3d mm = marker.position * 1000.0;
            const auto voxel = beam::gui::imagePositionToVoxelIndex(mm, mriAxes_);
            if (voxel.i == sagittal - 1) {
                const WorkflowMriMarker viewMarker{QPointF(1.0 - normalizedAxisPosition(mriAxes_.dimAP, mm.y()),
                                                            1.0 - normalizedAxisPosition(mriAxes_.dimIS, mm.z())),
                                                   markerLabel, QColor(230, 45, 55), false, draggable,
                                                   static_cast<int>(markerIndex)};
                if (ui_->registrationShowFiducialsCheckBox->isChecked()) sagMarkers.push_back(viewMarker);
                if (ui_->showFiducialsCheckBox->isChecked()) imagingSagMarkers.push_back(viewMarker);
            }
            if (voxel.j == coronal - 1) {
                const WorkflowMriMarker viewMarker{QPointF(normalizedAxisPosition(mriAxes_.dimLR, mm.x()),
                                                            1.0 - normalizedAxisPosition(mriAxes_.dimIS, mm.z())),
                                                   markerLabel, QColor(230, 45, 55), false, draggable,
                                                   static_cast<int>(markerIndex)};
                if (ui_->registrationShowFiducialsCheckBox->isChecked()) corMarkers.push_back(viewMarker);
                if (ui_->showFiducialsCheckBox->isChecked()) imagingCorMarkers.push_back(viewMarker);
            }
            if (voxel.k == axial - 1) {
                const WorkflowMriMarker viewMarker{QPointF(normalizedAxisPosition(mriAxes_.dimLR, mm.x()),
                                                            1.0 - normalizedAxisPosition(mriAxes_.dimAP, mm.y())),
                                                   markerLabel, QColor(230, 45, 55), false, draggable,
                                                   static_cast<int>(markerIndex)};
                if (ui_->registrationShowFiducialsCheckBox->isChecked()) axialMarkers.push_back(viewMarker);
                if (ui_->showFiducialsCheckBox->isChecked()) imagingAxialMarkers.push_back(viewMarker);
            }
        }
        const auto targetVoxel = beam::gui::imagePositionToVoxelIndex(targetMm_, mriAxes_);
        std::vector<WorkflowMriMarker> treatmentSagMarkers, treatmentCorMarkers, treatmentAxialMarkers;
        if (ui_->showTargetCheckBox->isChecked() && targetVoxel.i == sagittal - 1) {
            sagMarkers.push_back({QPointF(1.0 - normalizedAxisPosition(mriAxes_.dimAP, targetMm_.y()),
                                           1.0 - normalizedAxisPosition(mriAxes_.dimIS, targetMm_.z())),
                                  QString(), QColor(65, 235, 100), true});
            imagingSagMarkers.push_back(sagMarkers.back());
        }
        if (ui_->showTargetCheckBox->isChecked() && targetVoxel.j == coronal - 1) {
            corMarkers.push_back({QPointF(normalizedAxisPosition(mriAxes_.dimLR, targetMm_.x()),
                                           1.0 - normalizedAxisPosition(mriAxes_.dimIS, targetMm_.z())),
                                  QString(), QColor(65, 235, 100), true});
            imagingCorMarkers.push_back(corMarkers.back());
        }
        if (ui_->showTargetCheckBox->isChecked() && targetVoxel.k == axial - 1) {
            axialMarkers.push_back({QPointF(normalizedAxisPosition(mriAxes_.dimLR, targetMm_.x()),
                                             1.0 - normalizedAxisPosition(mriAxes_.dimAP, targetMm_.y())),
                                    QString(), QColor(65, 235, 100), true});
            imagingAxialMarkers.push_back(axialMarkers.back());
        }
        // Treatment targeting always keeps the selected RAS target visible,
        // independent of the Imaging/Registration overlay checkboxes.
        if (targetVoxel.i == sagittal - 1)
            treatmentSagMarkers.push_back({QPointF(1.0 - normalizedAxisPosition(mriAxes_.dimAP, targetMm_.y()),
                                                   1.0 - normalizedAxisPosition(mriAxes_.dimIS, targetMm_.z())),
                                           QStringLiteral("Selected target"), QColor(65, 235, 100), true});
        if (targetVoxel.j == coronal - 1)
            treatmentCorMarkers.push_back({QPointF(normalizedAxisPosition(mriAxes_.dimLR, targetMm_.x()),
                                                   1.0 - normalizedAxisPosition(mriAxes_.dimIS, targetMm_.z())),
                                           QStringLiteral("Selected target"), QColor(65, 235, 100), true});
        if (targetVoxel.k == axial - 1)
            treatmentAxialMarkers.push_back({QPointF(normalizedAxisPosition(mriAxes_.dimLR, targetMm_.x()),
                                                     1.0 - normalizedAxisPosition(mriAxes_.dimAP, targetMm_.y())),
                                             QStringLiteral("Selected target"), QColor(65, 235, 100), true});
        if (ui_->showFiducialsCheckBox->isChecked()) {
            for (std::size_t markerIndex = 0; markerIndex < fiducials_.size(); ++markerIndex) {
                const Eigen::Vector3d mm = fiducials_[markerIndex].position * 1000.0;
                const auto markerVoxel = beam::gui::imagePositionToVoxelIndex(mm, mriAxes_);
                const QString label = QStringLiteral("%1. %2").arg(markerIndex + 1)
                    .arg(QString::fromStdString(fiducials_[markerIndex].name));
                if (markerVoxel.i == sagittal - 1)
                    treatmentSagMarkers.push_back({QPointF(1.0 - normalizedAxisPosition(mriAxes_.dimAP, mm.y()),
                                                           1.0 - normalizedAxisPosition(mriAxes_.dimIS, mm.z())),
                                                   label, QColor(230, 45, 55), false, false, static_cast<int>(markerIndex)});
                if (markerVoxel.j == coronal - 1)
                    treatmentCorMarkers.push_back({QPointF(normalizedAxisPosition(mriAxes_.dimLR, mm.x()),
                                                           1.0 - normalizedAxisPosition(mriAxes_.dimIS, mm.z())),
                                                   label, QColor(230, 45, 55), false, false, static_cast<int>(markerIndex)});
                if (markerVoxel.k == axial - 1)
                    treatmentAxialMarkers.push_back({QPointF(normalizedAxisPosition(mriAxes_.dimLR, mm.x()),
                                                             1.0 - normalizedAxisPosition(mriAxes_.dimAP, mm.y())),
                                                     label, QColor(230, 45, 55), false, false, static_cast<int>(markerIndex)});
            }
        }
        ui_->sagittalPreview->setMarkers(std::move(imagingSagMarkers));
        ui_->coronalPreview->setMarkers(std::move(imagingCorMarkers));
        ui_->axialPreview->setMarkers(std::move(imagingAxialMarkers));
        ui_->registrationSagittalPreview->setMarkers(std::move(sagMarkers));
        ui_->registrationCoronalPreview->setMarkers(std::move(corMarkers));
        ui_->registrationAxialPreview->setMarkers(std::move(axialMarkers));
        treatmentSagittalPreview_->setMarkers(std::move(treatmentSagMarkers));
        treatmentCoronalPreview_->setMarkers(std::move(treatmentCorMarkers));
        treatmentAxialPreview_->setMarkers(std::move(treatmentAxialMarkers));
    } else {
        ui_->registrationSagittalPreview->setMaskOverlay(Eigen::MatrixXd{}, QColor(), 0.0);
        ui_->registrationCoronalPreview->setMaskOverlay(Eigen::MatrixXd{}, QColor(), 0.0);
        ui_->registrationAxialPreview->setMaskOverlay(Eigen::MatrixXd{}, QColor(), 0.0);
        ui_->registrationSagittalPreview->setSecondaryMaskOverlay(Eigen::MatrixXd{}, QColor(), 0.0);
        ui_->registrationCoronalPreview->setSecondaryMaskOverlay(Eigen::MatrixXd{}, QColor(), 0.0);
        ui_->registrationAxialPreview->setSecondaryMaskOverlay(Eigen::MatrixXd{}, QColor(), 0.0);
        ui_->registrationSagittalPreview->setMarkers({});
        ui_->registrationCoronalPreview->setMarkers({});
        ui_->registrationAxialPreview->setMarkers({});
        treatmentSagittalPreview_->setMarkers({});
        treatmentCoronalPreview_->setMarkers({});
        treatmentAxialPreview_->setMarkers({});
        const auto targetVoxel = beam::gui::imagePositionToVoxelIndex(targetMm_, mriAxes_);
        std::vector<WorkflowMriMarker> treatmentSagMarkers, treatmentCorMarkers, treatmentAxialMarkers;
        if (targetVoxel.i == sagittal - 1)
            treatmentSagMarkers.push_back({QPointF(1.0 - normalizedAxisPosition(mriAxes_.dimAP, targetMm_.y()),
                                                   1.0 - normalizedAxisPosition(mriAxes_.dimIS, targetMm_.z())),
                                           QStringLiteral("Selected target"), QColor(65, 235, 100), true});
        if (targetVoxel.j == coronal - 1)
            treatmentCorMarkers.push_back({QPointF(normalizedAxisPosition(mriAxes_.dimLR, targetMm_.x()),
                                                   1.0 - normalizedAxisPosition(mriAxes_.dimIS, targetMm_.z())),
                                           QStringLiteral("Selected target"), QColor(65, 235, 100), true});
        if (targetVoxel.k == axial - 1)
            treatmentAxialMarkers.push_back({QPointF(normalizedAxisPosition(mriAxes_.dimLR, targetMm_.x()),
                                                     1.0 - normalizedAxisPosition(mriAxes_.dimAP, targetMm_.y())),
                                             QStringLiteral("Selected target"), QColor(65, 235, 100), true});
        treatmentSagittalPreview_->setMarkers(std::move(treatmentSagMarkers));
        treatmentCoronalPreview_->setMarkers(std::move(treatmentCorMarkers));
        treatmentAxialPreview_->setMarkers(std::move(treatmentAxialMarkers));
    }

    const auto coordinate = [](const Eigen::VectorXd& axis, int oneBasedIndex) {
        const Eigen::Index i = std::clamp<Eigen::Index>(oneBasedIndex - 1, 0, axis.size() - 1);
        return axis(i);
    };
    const double lr = coordinate(mriAxes_.dimLR, sagittal);
    const double ap = coordinate(mriAxes_.dimAP, coronal);
    const double is = coordinate(mriAxes_.dimIS, axial);
    ui_->sagittalPreview->setRasMapping(QStringLiteral("sagittal"), lr,
                                        mriAxes_.dimAP(0), mriAxes_.dimAP(mriAxes_.dimAP.size() - 1),
                                        mriAxes_.dimIS(0), mriAxes_.dimIS(mriAxes_.dimIS.size() - 1), true, true);
    ui_->coronalPreview->setRasMapping(QStringLiteral("coronal"), ap,
                                       mriAxes_.dimLR(0), mriAxes_.dimLR(mriAxes_.dimLR.size() - 1),
                                       mriAxes_.dimIS(0), mriAxes_.dimIS(mriAxes_.dimIS.size() - 1), false, true);
    ui_->axialPreview->setRasMapping(QStringLiteral("axial"), is,
                                     mriAxes_.dimLR(0), mriAxes_.dimLR(mriAxes_.dimLR.size() - 1),
                                     mriAxes_.dimAP(0), mriAxes_.dimAP(mriAxes_.dimAP.size() - 1), false, true);
    ui_->registrationSagittalPreview->setRasMapping(QStringLiteral("sagittal"), lr, mriAxes_.dimAP(0), mriAxes_.dimAP(mriAxes_.dimAP.size() - 1), mriAxes_.dimIS(0), mriAxes_.dimIS(mriAxes_.dimIS.size() - 1), true, true);
    ui_->registrationCoronalPreview->setRasMapping(QStringLiteral("coronal"), ap, mriAxes_.dimLR(0), mriAxes_.dimLR(mriAxes_.dimLR.size() - 1), mriAxes_.dimIS(0), mriAxes_.dimIS(mriAxes_.dimIS.size() - 1), false, true);
    ui_->registrationAxialPreview->setRasMapping(QStringLiteral("axial"), is, mriAxes_.dimLR(0), mriAxes_.dimLR(mriAxes_.dimLR.size() - 1), mriAxes_.dimAP(0), mriAxes_.dimAP(mriAxes_.dimAP.size() - 1), false, true);
    treatmentSagittalPreview_->setRasMapping(QStringLiteral("sagittal"), lr, mriAxes_.dimAP(0), mriAxes_.dimAP(mriAxes_.dimAP.size() - 1), mriAxes_.dimIS(0), mriAxes_.dimIS(mriAxes_.dimIS.size() - 1), true, true);
    treatmentCoronalPreview_->setRasMapping(QStringLiteral("coronal"), ap, mriAxes_.dimLR(0), mriAxes_.dimLR(mriAxes_.dimLR.size() - 1), mriAxes_.dimIS(0), mriAxes_.dimIS(mriAxes_.dimIS.size() - 1), false, true);
    treatmentAxialPreview_->setRasMapping(QStringLiteral("axial"), is, mriAxes_.dimLR(0), mriAxes_.dimLR(mriAxes_.dimLR.size() - 1), mriAxes_.dimAP(0), mriAxes_.dimAP(mriAxes_.dimAP.size() - 1), false, true);
    const double lrCenter = coordinate(mriAxes_.dimLR, static_cast<int>((mriVolume_.nx - 1) / 2) + 1);
    const double apCenter = coordinate(mriAxes_.dimAP, static_cast<int>((mriVolume_.ny - 1) / 2) + 1);
    const double isCenter = coordinate(mriAxes_.dimIS, static_cast<int>((mriVolume_.nz - 1) / 2) + 1);
    ui_->sagittalSliceLabel->setText(QStringLiteral("LR %1 mm").arg(lr, 0, 'f', 1));
    ui_->coronalSliceLabel->setText(QStringLiteral("AP %1 mm").arg(ap, 0, 'f', 1));
    ui_->axialSliceLabel->setText(QStringLiteral("IS %1 mm").arg(is, 0, 'f', 1));
    ui_->registrationSagittalLabel->setText(QStringLiteral("LR %1 mm").arg(lr, 0, 'f', 1));
    ui_->registrationCoronalLabel->setText(QStringLiteral("AP %1 mm").arg(ap, 0, 'f', 1));
    ui_->registrationAxialLabel->setText(QStringLiteral("IS %1 mm").arg(is, 0, 'f', 1));
    ui_->sagittalSlider->setToolTip(QStringLiteral("LR %1 mm · %2 mm from initial center")
                                        .arg(lr, 0, 'f', 1).arg(lr - lrCenter, 0, 'f', 1));
    ui_->coronalSlider->setToolTip(QStringLiteral("AP %1 mm · %2 mm from initial center")
                                      .arg(ap, 0, 'f', 1).arg(ap - apCenter, 0, 'f', 1));
    ui_->axialSlider->setToolTip(QStringLiteral("IS %1 mm · %2 mm from initial center")
                                    .arg(is, 0, 'f', 1).arg(is - isCenter, 0, 'f', 1));
}

void WorkflowWindow::selectStage(beam::gui::WorkflowStage stage) {
    selectedStage_ = stage;
    ui_->pageTitle->setText(QString::fromUtf8(beam::gui::workflowStageName(stage).data()));
    const int stageIndex = static_cast<int>(stage);
    ui_->pageStack->setCurrentIndex(stageIndex <= 3 ? stageIndex : 4);
    ui_->workflowMessage->setVisible(stage != beam::gui::WorkflowStage::Registration);
    ui_->completeStageButton->setVisible(stage != beam::gui::WorkflowStage::CaseSetup &&
                                         stage != beam::gui::WorkflowStage::SystemCheck &&
                                         stage != beam::gui::WorkflowStage::Imaging &&
                                         stage != beam::gui::WorkflowStage::Registration &&
                                         stage != beam::gui::WorkflowStage::Coupling &&
                                         stage != beam::gui::WorkflowStage::Correction &&
                                         stage != beam::gui::WorkflowStage::TreatmentPlan &&
                                         stage != beam::gui::WorkflowStage::SafetyReview &&
                                         stage != beam::gui::WorkflowStage::Treatment);
    const bool couplingPage = stage == beam::gui::WorkflowStage::Coupling ||
                              stage == beam::gui::WorkflowStage::Correction;
    const bool correctionPage = false;
    const bool treatmentPlanPage = stage == beam::gui::WorkflowStage::TreatmentPlan;
    const bool treatmentExecutionPage = stage == beam::gui::WorkflowStage::Treatment;
    const bool safetyReviewPage = stage == beam::gui::WorkflowStage::SafetyReview;
    for (QWidget* widget : {static_cast<QWidget*>(couplingTitleLabel_),
                            static_cast<QWidget*>(couplingDescriptionLabel_),
                            static_cast<QWidget*>(couplingStatusLabel_),
                            static_cast<QWidget*>(couplingProgressBar_),
                            static_cast<QWidget*>(runCouplingCheckButton_),
                            static_cast<QWidget*>(acceptCouplingButton_),
                            static_cast<QWidget*>(correctionTitleLabel_),
                            static_cast<QWidget*>(correctionDescriptionLabel_),
                            static_cast<QWidget*>(correctionStatusLabel_),
                            static_cast<QWidget*>(correctionProgressBar_),
                            static_cast<QWidget*>(runCorrectionButton_),
                            static_cast<QWidget*>(acceptCorrectionButton_),
                            static_cast<QWidget*>(treatmentPlanTitleLabel_),
                            static_cast<QWidget*>(treatmentPlanDescriptionLabel_),
                            static_cast<QWidget*>(treatmentPlanTargetLabel_),
                            static_cast<QWidget*>(treatmentProtocolLabel_),
                            static_cast<QWidget*>(treatmentProtocolCombo_),
                            static_cast<QWidget*>(treatmentTargetTableLabel_),
                            static_cast<QWidget*>(treatmentTargetTable_),
                            static_cast<QWidget*>(treatmentProtocolTableLabel_),
                            static_cast<QWidget*>(treatmentProtocolTable_),
                            static_cast<QWidget*>(addSonicationButton_),
                            static_cast<QWidget*>(removeSonicationButton_),
                            static_cast<QWidget*>(acceptTreatmentPlanButton_),
                            static_cast<QWidget*>(treatmentExecutionTitleLabel_),
                            static_cast<QWidget*>(treatmentExecutionDescriptionLabel_),
                            static_cast<QWidget*>(treatmentExecutionStatusLabel_),
                            static_cast<QWidget*>(treatmentExecutionProgressBar_),
                            static_cast<QWidget*>(startTreatmentButton_),
                            static_cast<QWidget*>(abortTreatmentButton_),
                            static_cast<QWidget*>(treatmentBodySplitter_),
                            static_cast<QWidget*>(treatmentTabs_),
                            static_cast<QWidget*>(safetyReviewTitleLabel_),
                            static_cast<QWidget*>(safetyReviewDescriptionLabel_),
                            static_cast<QWidget*>(safetyReviewStatusLabel_),
                            static_cast<QWidget*>(safetyRegistrationCheckBox_),
                            static_cast<QWidget*>(safetyCorrectionCheckBox_),
                            static_cast<QWidget*>(safetyPlanCheckBox_),
                            static_cast<QWidget*>(acceptSafetyReviewButton_)}) {
        if (!widget) continue;
        const bool isCorrectionWidget = widget == correctionTitleLabel_ || widget == correctionDescriptionLabel_ ||
                                        widget == correctionStatusLabel_ || widget == correctionProgressBar_ ||
                                        widget == runCorrectionButton_ || widget == acceptCorrectionButton_;
        const bool isTreatmentWidget = widget == treatmentPlanTitleLabel_ || widget == treatmentPlanDescriptionLabel_ ||
                                       widget == treatmentPlanTargetLabel_ || widget == treatmentProtocolLabel_ ||
                                       widget == treatmentProtocolCombo_ || widget == treatmentTargetTableLabel_ ||
                                       widget == treatmentTargetTable_ || widget == treatmentProtocolTableLabel_ ||
                                       widget == treatmentProtocolTable_ || widget == addSonicationButton_ ||
                                       widget == removeSonicationButton_ ||
                                       widget == acceptTreatmentPlanButton_ || widget == treatmentTabs_ ||
                                       widget == treatmentBodySplitter_;
        const bool isExecutionWidget = widget == treatmentExecutionTitleLabel_ ||
                                       widget == treatmentExecutionDescriptionLabel_ ||
                                       widget == treatmentExecutionStatusLabel_ ||
                                       widget == treatmentExecutionProgressBar_ ||
                                       widget == startTreatmentButton_ || widget == abortTreatmentButton_;
        const bool isSafetyWidget = widget == safetyReviewTitleLabel_ || widget == safetyReviewDescriptionLabel_ ||
                                    widget == safetyReviewStatusLabel_ || widget == safetyRegistrationCheckBox_ ||
                                    widget == safetyCorrectionCheckBox_ || widget == safetyPlanCheckBox_ ||
                                    widget == acceptSafetyReviewButton_;
        widget->setVisible(isSafetyWidget ? safetyReviewPage :
                           (isExecutionWidget ? treatmentExecutionPage :
                           (isTreatmentWidget ? treatmentPlanPage : (isCorrectionWidget ? correctionPage : couplingPage))));
    }
    if (ui_->placeholderText) ui_->placeholderText->setVisible(!couplingPage && !correctionPage && !treatmentPlanPage &&
                                                               !treatmentExecutionPage && !safetyReviewPage);
    if (treatmentPlanPage && treatmentPlanTargetLabel_) {
        treatmentPlanTargetLabel_->setText(QStringLiteral("Registered target (RAS): LR %1 mm, AP %2 mm, IS %3 mm")
                                               .arg(targetMm_.x(), 0, 'f', 2)
                                               .arg(targetMm_.y(), 0, 'f', 2)
                                               .arg(targetMm_.z(), 0, 'f', 2));
        treatmentTargetTable_->item(0, 1)->setText(QString::number(targetMm_.x(), 'f', 2));
        treatmentTargetTable_->item(0, 2)->setText(QString::number(targetMm_.y(), 'f', 2));
        treatmentTargetTable_->item(0, 3)->setText(QString::number(targetMm_.z(), 'f', 2));
        acceptTreatmentPlanButton_->setEnabled(
            workflow_.state(beam::gui::WorkflowStage::Correction).status == beam::gui::WorkflowStatus::Complete);
    }
    if (safetyReviewPage) {
        const auto complete = [this](beam::gui::WorkflowStage checkStage) {
            return workflow_.state(checkStage).status == beam::gui::WorkflowStatus::Complete;
        };
        const bool registrationOk = complete(beam::gui::WorkflowStage::Registration);
        const bool correctionOk = complete(beam::gui::WorkflowStage::Correction);
        const bool planOk = complete(beam::gui::WorkflowStage::TreatmentPlan);
        safetyRegistrationCheckBox_->setChecked(registrationOk);
        safetyCorrectionCheckBox_->setChecked(correctionOk);
        safetyPlanCheckBox_->setChecked(planOk);
        acceptSafetyReviewButton_->setEnabled(registrationOk && correctionOk && planOk);
        safetyReviewStatusLabel_->setText(registrationOk && correctionOk && planOk
            ? QStringLiteral("All safety prerequisites are complete. Review and accept to continue.")
            : QStringLiteral("One or more prerequisites are incomplete; treatment remains locked."));
    }
    if (stage == beam::gui::WorkflowStage::Registration && mriLoaded_) {
        showMriPreviews();
        const int row = ui_->registrationTable->currentRow();
        if (row >= 0) {
            navigateToRegistrationFiducial(row);
            if (registrationStartFiducialRow_ < 0) {
                registrationStartFiducialRow_ = row;
                ui_->registrationReturnStartButton->setEnabled(true);
            }
        }
    }
    if (stage == beam::gui::WorkflowStage::Registration) {
        for (QLabel* label : {registrationStep1Label_, registrationStep2Label_,
                              registrationStep3Label_, registrationStep4Label_}) {
            if (label) {
                label->show();
                label->raise();
            }
        }
        if (registrationPhase_ == RegistrationPhase::Accepted) {
            // Returning to a completed Registration page is a review state;
            // do not present Step 3/4 as if they still require action.
        }
    }
    refresh();
    if (stage == beam::gui::WorkflowStage::Registration) {
        if (calibrationGroup_) calibrationGroup_->setEnabled(true);
        updateRegistrationAvailability();
    }
    QTimer::singleShot(0, this, [this] { syncRegistrationPreviewHeights(); });
}

void WorkflowWindow::completeCurrentStage() {
    if (selectedStage_ == beam::gui::WorkflowStage::CaseSetup &&
        (ui_->participantEdit->text().trimmed().isEmpty() || ui_->siteEdit->text().trimmed().isEmpty() ||
         !ui_->visitEdit->hasAcceptableInput())) {
        showMessage(QStringLiteral("Participant ID, Site ID, and Visit are required."), true);
        return;
    }
    std::string reason;
    if (!workflow_.complete(selectedStage_, &reason)) {
        showMessage(QString::fromStdString(reason), true);
        return;
    }
    showMessage(QStringLiteral("%1 complete.").arg(QString::fromUtf8(beam::gui::workflowStageName(selectedStage_).data())), false);
    const auto next = workflow_.nextStage();
    ui_->stageList->setCurrentRow(static_cast<int>(next));
    refresh();
}

void WorkflowWindow::refresh() {
    for (std::size_t i = 0; i < stageCount; ++i) {
        const auto stage = static_cast<beam::gui::WorkflowStage>(i);
        const auto& state = workflow_.state(stage);
        ui_->stageList->item(static_cast<int>(i))->setText(
            QStringLiteral("%1  %2\n    %3")
                .arg(statusSymbol(state.status), QString::fromUtf8(beam::gui::workflowStageName(stage).data()),
                     QString::fromUtf8(beam::gui::workflowStatusName(state.status).data())));
    }
    std::string reason;
    const bool ready = workflow_.canStartTreatment(&reason);
    ui_->abortButton->setEnabled(workflow_.state(beam::gui::WorkflowStage::Treatment).status ==
                                  beam::gui::WorkflowStatus::InProgress);
    if (selectedStage_ == beam::gui::WorkflowStage::Treatment && !ready) showMessage(QString::fromStdString(reason), true);
}

void WorkflowWindow::showMessage(const QString& text, bool error) {
    ui_->workflowMessage->setText(text);
    ui_->workflowMessage->setStyleSheet(error
        ? QStringLiteral("padding: 10px; color: #845006; background: #fff4dc; border-radius: 5px;")
        : QStringLiteral("padding: 10px; color: #12663f; background: #e8f5ed; border-radius: 5px;"));
}

}  // namespace beam::app
