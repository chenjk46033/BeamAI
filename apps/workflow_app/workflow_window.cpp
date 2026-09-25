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
#include <utility>

#include <QListWidgetItem>
#include <QFileDialog>
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
#include <QSlider>
#include <QLabel>

#include "infra_dicom/load_mri_ras.hpp"
#include "infra_mat/legacy_beam_session.hpp"
#include "array/array_data.hpp"
#include "array/array_struct.hpp"
#include "gui/mri_overlay_presenter.hpp"
#include "mri/mri_loader.hpp"
#include "mri/ras_transform.hpp"
#include "mri_view.hpp"
#include "registration/array_transform.hpp"
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
    auto* triangleStep = new QLabel(QStringLiteral("1"), markerDiagramPanel);
    triangleStep->setStyleSheet(QStringLiteral(
        "QLabel { color: rgba(230, 240, 244, 150); background: transparent; font-size: 24px; font-weight: 700; }"));
    triangleStep->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    triangleStep->setFixedHeight(28);
    triangleColumn->addWidget(triangleStep);
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
        layout->setSpacing(2);
    }
    for (QLabel* label : {ui_->registrationSagittalLabel,
                          ui_->registrationCoronalLabel,
                          ui_->registrationAxialLabel}) {
        label->setMaximumHeight(18);
        label->setContentsMargins(0, 0, 0, 0);
    }
    for (QVBoxLayout* layout : {ui_->sagittalLayout, ui_->coronalLayout, ui_->axialLayout}) {
        layout->setContentsMargins(4, 3, 4, 2);
        layout->setSpacing(2);
    }
    for (QLabel* label : {ui_->sagittalSliceLabel, ui_->coronalSliceLabel, ui_->axialSliceLabel}) {
        label->setMaximumHeight(18);
        label->setContentsMargins(0, 0, 0, 0);
    }
    auto* markerTablePanel = new QWidget(markerSplitter);
    markerTablePanel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto* markerTableLayout = new QVBoxLayout(markerTablePanel);
    markerTableLayout->setContentsMargins(0, 0, 0, 0);
    markerTableLayout->setSpacing(0);
    ui_->registrationActions->removeWidget(ui_->registerFiducialsButton);
    auto* tableStep = new QLabel(QStringLiteral("2"), markerTablePanel);
    tableStep->setStyleSheet(QStringLiteral(
        "QLabel { color: rgba(230, 240, 244, 150); background: transparent; font-size: 24px; font-weight: 700; }"));
    tableStep->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    tableStep->setFixedHeight(28);
    markerTableLayout->addWidget(tableStep);
    ui_->confirmFiducialButton->setText(QStringLiteral("Confirm all located fiducials"));
    ui_->confirmFiducialButton->setToolTip(QStringLiteral(
        "Confirm every fiducial currently marked Located after reviewing the MRI views"));
    ui_->confirmFiducialButton->setMinimumHeight(30);
    ui_->confirmFiducialButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    ui_->registerFiducialsButton->setMinimumHeight(30);
    ui_->registerFiducialsButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto* markerActions = new QHBoxLayout;
    markerActions->setContentsMargins(0, 0, 0, 0);
    markerActions->setSpacing(4);
    markerActions->addWidget(ui_->confirmFiducialButton, 1);
    markerActions->addWidget(ui_->registerFiducialsButton, 1);
    markerTableLayout->addWidget(ui_->registrationTable, 1);
    markerTableLayout->addLayout(markerActions);
    ui_->registrationLayout->removeWidget(ui_->acceptRegistrationButton);
    ui_->acceptRegistrationButton->setMinimumHeight(30);
    auto* acceptStep = new QLabel(QStringLiteral("4"), markerTablePanel);
    acceptStep->setStyleSheet(QStringLiteral(
        "QLabel { color: rgba(230, 240, 244, 150); background: transparent; font-size: 24px; font-weight: 700; }"));
    acceptStep->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    acceptStep->setFixedHeight(28);
    markerTableLayout->addWidget(acceptStep);
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
    auto* calibrationGroup = new QGroupBox(QStringLiteral("Array lock-position calibration"), this);
    auto* calibrationLayout = new QHBoxLayout(calibrationGroup);
    calibrationLayout->setContentsMargins(8, 4, 8, 4);
    calibrationLayout->setSpacing(8);
    auto* calibrationStep = new QLabel(QStringLiteral("3"), calibrationGroup);
    calibrationStep->setStyleSheet(QStringLiteral(
        "QLabel { color: rgba(230, 240, 244, 150); background: transparent; font-size: 24px; font-weight: 700; }"));
    calibrationStep->setAlignment(Qt::AlignCenter);
    calibrationLayout->addWidget(calibrationStep, 0);
    auto makePositionSlider = [](QWidget* parent, Qt::Orientation orientation) {
        auto* slider = new QSlider(orientation, parent);
        slider->setRange(1, 10);
        slider->setValue(1);
        slider->setSingleStep(1);
        slider->setPageStep(1);
        slider->setToolTip(QStringLiteral("Array lock position; each step is 7.5 mm horizontal or 10 mm vertical"));
        return slider;
    };
    auto makeLockPanel = [&](const QString& title, QSlider*& horizontal, QSlider*& vertical) {
        auto* panel = new QGroupBox(title, calibrationGroup);
        auto* panelLayout = new QGridLayout(panel);
        panelLayout->setContentsMargins(5, 2, 5, 2);
        panelLayout->setSpacing(1);
        vertical = makePositionSlider(panel, Qt::Vertical);
        horizontal = makePositionSlider(panel, Qt::Horizontal);
        vertical->setFixedHeight(44);
        panelLayout->addWidget(new QLabel(QStringLiteral("V"), panel), 0, 0, Qt::AlignCenter);
        panelLayout->addWidget(vertical, 1, 0, Qt::AlignCenter);
        panelLayout->addWidget(new QLabel(QStringLiteral("H"), panel), 1, 1, Qt::AlignCenter);
        panelLayout->addWidget(horizontal, 2, 1);
        return panel;
    };
    calibrationLayout->addWidget(makeLockPanel(QStringLiteral("Subject Left"),
                                                leftHorizontalPositionSlider_, leftVerticalPositionSlider_), 0);
    calibrationLayout->addWidget(makeLockPanel(QStringLiteral("Subject Right"),
                                                rightHorizontalPositionSlider_, rightVerticalPositionSlider_), 0);
    registerCurrentPositionButton_ = new QPushButton(QStringLiteral("Register Arrays to Current Position"), calibrationGroup);
    registerCurrentPositionButton_->setMinimumHeight(30);
    registerCurrentPositionButton_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    registerCurrentPositionButton_->setToolTip(QStringLiteral(
        "Apply the BeamV0 lock-position offset after registering the array to MRI fiducials"));
    calibrationLayout->addWidget(registerCurrentPositionButton_, 0);
    calibrationGroup->setMinimumHeight(0);
    calibrationGroup->setMaximumHeight(QWIDGETSIZE_MAX);
    calibrationGroup->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    markerDiagramLayout->addWidget(calibrationGroup, 1);
    // Registration feedback belongs inside the Registration page, below its
    // acceptance action, rather than in the global footer line.
    ui_->registrationResult->show();
    markerTableLayout->addWidget(ui_->registrationResult);
    auto invalidateCurrentPositionRegistration = [this] {
        currentPositionRegistrationComplete_ = false;
        updateRegistrationAvailability();
    };
    for (QSlider* slider : {leftHorizontalPositionSlider_, leftVerticalPositionSlider_,
                            rightHorizontalPositionSlider_, rightVerticalPositionSlider_})
        connect(slider, &QSlider::valueChanged, this, [invalidateCurrentPositionRegistration] {
            invalidateCurrentPositionRegistration();
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

    connect(ui_->stageList, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row >= 0 && row < static_cast<int>(stageCount)) selectStage(static_cast<beam::gui::WorkflowStage>(row));
    });
    connect(ui_->completeStageButton, &QPushButton::clicked, this, [this] { completeCurrentStage(); });
    connect(ui_->acceptCaseButton, &QPushButton::clicked, this, [this] { completeCurrentStage(); });
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
        registrationComplete_ = false;
        pendingRegistrationFit_ = false;
        ui_->acceptRegistrationButton->setEnabled(false);
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
                bool hasPendingLocated = false;
                for (std::size_t index = 0; index < fiducialLocated_.size(); ++index)
                    hasPendingLocated = hasPendingLocated || (fiducialLocated_[index] && !fiducialConfirmed_[index]);
                ui_->confirmFiducialButton->setEnabled(hasPendingLocated);
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
        ui_->registrationTable->setCurrentCell(markerIndex, 0);
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
        registrationComplete_ = false;
        pendingRegistrationFit_ = false;
        ui_->acceptRegistrationButton->setEnabled(false);
        ui_->registrationResult->setText(QStringLiteral("Measurements changed. Run registration again to accept them."));
        refresh();
        ui_->registrationTable->blockSignals(true);
        ui_->registrationTable->item(row, 4)->setText(QStringLiteral("Located"));
        ui_->confirmFiducialButton->setEnabled(true);
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
    ui_->registrationTable->verticalHeader()->setDefaultSectionSize(25);
    ui_->registrationTable->verticalHeader()->setMinimumSectionSize(22);
    // Six complete rows plus the header, without an oversized viewport.
    ui_->registrationTable->setFixedHeight(180);
    ui_->registrationTable->setStyleSheet(
        QStringLiteral("QTableWidget { color: #17313f; background: #ffffff; } "
                       "QTableWidget::item { color: #17313f; background: #ffffff; } "
                       "QTableWidget::item:alternate { color: #17313f; background: #f1f6f8; } "
                       "QTableWidget::item:selected, QTableWidget::item:selected:!active "
                       "{ background: #58c7e5; color: #083d4d; }"));
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
    const int referenceHeight = ui_->sagittalPreview->height();
    if (referenceHeight < 300) return;
    // Account for the group margins, slice slider row, and coordinate label
    // below each preview.  Keep a generous reserve for the four tool buttons
    // in the slider row; otherwise the details panel can be laid out over the
    // lower half of those buttons after a maximize/restore cycle.
    const int registrationGroupHeight = referenceHeight + 72;
    // Do not let the three-image row negotiate itself below the child layouts.
    // This is especially important when the stacked page becomes visible
    // after a window-state change.
    ui_->registrationImages->setSizeConstraint(QLayout::SetMinimumSize);
    for (QGroupBox* group : {ui_->registrationSagittalGroup,
                             ui_->registrationCoronalGroup,
                             ui_->registrationAxialGroup}) {
        group->setMinimumHeight(registrationGroupHeight);
    }
    for (WorkflowMriView* view : {ui_->registrationSagittalPreview,
                                  ui_->registrationCoronalPreview,
                                  ui_->registrationAxialPreview}) {
        if (!view || view->minimumHeight() == referenceHeight &&
                      view->maximumHeight() == referenceHeight) continue;
        view->setMinimumHeight(referenceHeight);
        view->setMaximumHeight(referenceHeight);
    }
}

WorkflowWindow::~WorkflowWindow() { delete ui_; }

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
    ui_->sagittalSlider->setValue(static_cast<int>((mriVolume_.nx - 1) / 2));
    ui_->coronalSlider->setValue(static_cast<int>((mriVolume_.ny - 1) / 2));
    ui_->axialSlider->setValue(static_cast<int>((mriVolume_.nz - 1) / 2));
    for (QSlider* slider : {ui_->sagittalSlider, ui_->coronalSlider, ui_->axialSlider,
                            ui_->registrationSagittalSlider, ui_->registrationCoronalSlider, ui_->registrationAxialSlider}) slider->setEnabled(true);
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
        ui_->sagittalSlider->setValue(static_cast<int>((mriVolume_.nx - 1) / 2));
        ui_->coronalSlider->setValue(static_cast<int>((mriVolume_.ny - 1) / 2));
        ui_->axialSlider->setValue(static_cast<int>((mriVolume_.nz - 1) / 2));
        ui_->sagittalSlider->setEnabled(true);
        ui_->coronalSlider->setEnabled(true);
        ui_->axialSlider->setEnabled(true);
        ui_->registrationSagittalSlider->setEnabled(true);
        ui_->registrationCoronalSlider->setEnabled(true);
        ui_->registrationAxialSlider->setEnabled(true);
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
                                  ui_->registrationAxialPreview})
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
    registrationComplete_ = false;
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
    ui_->confirmFiducialButton->setEnabled(hasPendingLocated);
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
    pendingRegistrationFit_ = false;
    registrationComplete_ = false;
    ui_->acceptRegistrationButton->setEnabled(false);
    if (placingFiducial_) setPlacementMode(false);
    ui_->registrationTable->item(row, 4)->setText(QStringLiteral("Located"));
    ui_->confirmFiducialButton->setEnabled(true);
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
    ui_->confirmFiducialButton->setEnabled(false);
    pendingRegistrationFit_ = false;
    registrationComplete_ = false;
    ui_->acceptRegistrationButton->setEnabled(false);
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
    const bool allMeasured = std::all_of(fiducialConfirmed_.begin(), fiducialConfirmed_.end(), [](bool v) { return v; });
    const bool imagingAccepted = workflow_.state(beam::gui::WorkflowStage::Imaging).status == beam::gui::WorkflowStatus::Complete;
    ui_->registerFiducialsButton->setEnabled(registrationGeometryLoaded_ && allMeasured && imagingAccepted);
    if (registerCurrentPositionButton_)
        registerCurrentPositionButton_->setEnabled(registrationGeometryLoaded_ && allMeasured && imagingAccepted &&
                                                    registrationComplete_ && !currentPositionRegistrationComplete_);
}

void WorkflowWindow::applyRegistrationResult(beam::registration::AffineArrayResult result) {
    arrayData_ = std::move(result.arrayData);
    fiducials_ = std::move(result.fiducialMarkers);
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
        double squaredError = 0.0;
        double maximumError = -1.0;
        int worstRow = -1;
        std::array<double, 6> residuals{};
        for (std::size_t i = 0; i < measured.size(); ++i) {
            residuals[i] = (result.fiducialMarkers[i].position * 1000.0 - measured[i]).norm();
            squaredError += residuals[i] * residuals[i];
            if (residuals[i] > maximumError) { maximumError = residuals[i]; worstRow = static_cast<int>(i); }
        }
        const double rmsMm = std::sqrt(squaredError / static_cast<double>(measured.size()));
        applyRegistrationResult(std::move(result));
        ui_->registrationTable->blockSignals(true);
        for (int row = 0; row < 6; ++row) {
            QTableWidgetItem* item = ui_->registrationTable->item(row, 5);
            item->setText(QString::number(residuals[static_cast<std::size_t>(row)], 'f', 2));
            const bool worst = row == worstRow;
            item->setBackground(worst ? QColor(255, 220, 205) : QColor(Qt::transparent));
            item->setForeground(worst ? QColor(150, 35, 20) : QColor());
            item->setToolTip(worst ? QStringLiteral("Largest residual") : QString());
        }
        ui_->registrationTable->blockSignals(false);
        pendingRegistrationFit_ = true;
        registrationComplete_ = false;
        ui_->acceptRegistrationButton->setEnabled(true);
        ui_->registrationResult->setText(QStringLiteral("Registration complete · RMS residual %1 mm · overlays updated")
                                             .arg(rmsMm, 0, 'f', 2));
        showMessage(QStringLiteral("Registration complete — RMS residual %1 mm. Review the updated red fiducials and yellow transducer overlay.")
                        .arg(rmsMm, 0, 'f', 2), false);
        const QString worstName = ui_->registrationTable->item(worstRow, 0)->text();
        ui_->registrationResult->setText(QStringLiteral("Fit ready for review: RMS %1 mm, maximum %2 mm (%3)")
                                             .arg(rmsMm, 0, 'f', 2)
                                             .arg(maximumError, 0, 'f', 2)
                                             .arg(worstName));
        showMessage(QStringLiteral("Fit calculated: RMS %1 mm; maximum %2 mm at %3. Review residuals and overlays, then accept or edit a marker.")
                        .arg(rmsMm, 0, 'f', 2)
                        .arg(maximumError, 0, 'f', 2)
                        .arg(worstName), false);
        refresh();
    } catch (const std::exception& error) {
        ui_->registrationResult->setText(QStringLiteral("Registration failed: %1").arg(QString::fromUtf8(error.what())));
        showMessage(QStringLiteral("Registration failed. Verify that all six points are distinct and correctly paired."), true);
    }
}

void WorkflowWindow::acceptFiducialRegistration() {
    if (!pendingRegistrationFit_) {
        showMessage(QStringLiteral("Calculate and review a fit before accepting registration."), true);
        return;
    }
    std::string reason;
    if (!workflow_.complete(beam::gui::WorkflowStage::Registration, &reason)) {
        showMessage(QString::fromStdString(reason), true);
        return;
    }
    pendingRegistrationFit_ = false;
    registrationComplete_ = true;
    ui_->acceptRegistrationButton->setEnabled(false);
    ui_->registrationResult->setText(ui_->registrationResult->text() + QStringLiteral("; accepted"));
    showMessage(QStringLiteral("Registration accepted. Continuing to the next workflow stage."), false);
    refresh();
    ui_->stageList->setCurrentRow(static_cast<int>(workflow_.nextStage()));
}

void WorkflowWindow::performCurrentPositionRegistration() {
    if (!registrationComplete_ || fiducials_.size() != 6) {
        showMessage(QStringLiteral("Accept the MRI-fiducial registration before calibrating the current array position."), true);
        return;
    }
    if (leftHorizontalPositionSlider_->value() != rightHorizontalPositionSlider_->value() ||
        leftVerticalPositionSlider_->value() != rightVerticalPositionSlider_->value()) {
        showMessage(QStringLiteral("Subject Left and Subject Right lock-position sliders must match."), true);
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
        currentPositionRegistrationComplete_ = true;
        registerCurrentPositionButton_->setEnabled(false);
        showMessage(QStringLiteral("Array registered to current lock position."), false);
        refresh();
    } catch (const std::exception& error) {
        showMessage(QStringLiteral("Current-position registration failed: %1")
                        .arg(QString::fromUtf8(error.what())), true);
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
    const double iNorm = mriVolume_.nx > 1 ? static_cast<double>(sagittal - 1) / (mriVolume_.nx - 1) : 0.5;
    const double jNorm = mriVolume_.ny > 1 ? static_cast<double>(coronal - 1) / (mriVolume_.ny - 1) : 0.5;
    const double kNorm = mriVolume_.nz > 1 ? static_cast<double>(axial - 1) / (mriVolume_.nz - 1) : 0.5;
    ui_->sagittalPreview->setNavigationCrosshair(QPointF(1.0 - jNorm, 1.0 - kNorm));
    ui_->coronalPreview->setNavigationCrosshair(QPointF(iNorm, 1.0 - kNorm));
    ui_->axialPreview->setNavigationCrosshair(QPointF(iNorm, 1.0 - jNorm));
    ui_->registrationSagittalPreview->setNavigationCrosshair(QPointF(1.0 - jNorm, 1.0 - kNorm));
    ui_->registrationCoronalPreview->setNavigationCrosshair(QPointF(iNorm, 1.0 - kNorm));
    ui_->registrationAxialPreview->setNavigationCrosshair(QPointF(iNorm, 1.0 - jNorm));

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
        ui_->sagittalPreview->setMarkers(std::move(imagingSagMarkers));
        ui_->coronalPreview->setMarkers(std::move(imagingCorMarkers));
        ui_->axialPreview->setMarkers(std::move(imagingAxialMarkers));
        ui_->registrationSagittalPreview->setMarkers(std::move(sagMarkers));
        ui_->registrationCoronalPreview->setMarkers(std::move(corMarkers));
        ui_->registrationAxialPreview->setMarkers(std::move(axialMarkers));
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
                                         stage != beam::gui::WorkflowStage::Registration);
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
    refresh();
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
