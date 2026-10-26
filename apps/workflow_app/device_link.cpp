// Mounts the device-link panel and wires it to the shell header.

#include <QChar>
#include <QLabel>
#include <QLatin1String>
#include <QDir>
#include <QVBoxLayout>

#include <Eigen/Core>

#include "device_link_panel.hpp"
#include "ui_workflow_shell.h"
#include "workflow_window.hpp"

namespace beam::app {

void WorkflowWindow::buildDeviceLinkPanel() {
    loadCalibrationCurve();
    deviceLink_ = new DeviceLinkPanel(ui_->placeholderPage);
    ui_->placeholderLayout->addWidget(deviceLink_);

    serialHeaderLabel_ = ui_->deviceHeader;
    connect(deviceLink_, &DeviceLinkPanel::linkLostWhileDelivering, this,
            [this](const QString& text) {
                showMessage(QStringLiteral("Lost contact with the device while delivering: %1 "
                                           "Ultrasound may still be going out -- Abort cannot "
                                           "stop it.")
                                .arg(text),
                            true);
            });
    connect(deviceLink_, &DeviceLinkPanel::linkStateChanged, this,
            [this](bool healthy, const QString& text) {
                serialHeaderLabel_->setText(QString(QChar(0x25CF)) + QLatin1Char(' ') + text);
                serialHeaderLabel_->setStyleSheet(
                    healthy ? QStringLiteral("color: #6ee7a0; padding-left: 28px;")
                            : QStringLiteral("color: #ff8290; padding-left: 28px;"));
                if (healthy && mcuConnectionInvalid_) {
                    mcuConnectionInvalid_ = false;
                    setSonicateEnabled(sonicateEnabled_);
                }
            });
}

}  // namespace beam::app

namespace {

std::string joinValues(const Eigen::VectorXd& values) {
    std::string text;
    for (Eigen::Index i = 0; i < values.size(); ++i) {
        if (i != 0) text += ';';
        text += QString::number(values(i)).toStdString();
    }
    return text;
}

}  // namespace

namespace beam::app {

// The curve that turns a wanted pressure into a duty cycle. A file, so the
// value delivered can be traced to the calibration in force at the time.
void WorkflowWindow::loadCalibrationCurve() {
    calibration_ = beam::stimulation::defaultCalibrationCurve();
    calibrationFromFile_ = false;
    const QString path =
        QDir(treatmentDataDirectory()).filePath(QStringLiteral("transducer_calibration.csv"));
    try {
        calibration_ = beam::stimulation::readCalibrationCurve(path.toStdString());
        calibrationFromFile_ = true;
    } catch (const std::exception& e) {
        showMessage(QStringLiteral("Calibration curve not read (%1). Using the built-in values.")
                        .arg(QString::fromStdString(e.what())),
                    true);
    }
}

std::string WorkflowWindow::calibrationDutyText() const { return joinValues(calibration_.dutyPercent); }
std::string WorkflowWindow::calibrationPressureText() const { return joinValues(calibration_.pressureMpa); }

}  // namespace beam::app
