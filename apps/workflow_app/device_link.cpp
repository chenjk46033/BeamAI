// Mounts the device-link panel and wires it to the shell header.

#include <QChar>
#include <QLabel>
#include <QLatin1String>
#include <QVBoxLayout>

#include "device_link_panel.hpp"
#include "ui_workflow_shell.h"
#include "workflow_window.hpp"

namespace beam::app {

void WorkflowWindow::buildDeviceLinkPanel() {
    deviceLink_ = new DeviceLinkPanel(ui_->placeholderPage);
    ui_->placeholderLayout->addWidget(deviceLink_);

    serialHeaderLabel_ = ui_->deviceHeader;
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
