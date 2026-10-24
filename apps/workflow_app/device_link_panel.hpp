#pragma once

// Owns the link to the device: the port, the polling thread, and the
// widgets that report it.

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <QWidget>

namespace beam::serialcom {
class LinkMonitor;
class SerialLink;
struct LinkEvent;
}  // namespace beam::serialcom

QT_BEGIN_NAMESPACE
class QLabel;
class QListWidget;
class QPushButton;
QT_END_NAMESPACE

namespace beam::app {

class DeviceLinkPanel final : public QWidget {
    Q_OBJECT

public:
    explicit DeviceLinkPanel(QWidget* parent = nullptr);
    ~DeviceLinkPanel() override;

    // What the last poll saw, so up to one interval stale.
    bool isHealthy() const { return healthy_; }

    // Asks now rather than trusting the last poll. Use before delivering.
    bool askDevice();

    // Runs `use` on the open port with the polling thread held off. False
    // when there is no link, and then `use` is not called.
    bool withLink(const std::function<void(beam::serialcom::SerialLink&)>& use);

    // Sends `cmd` from a worker thread and calls `done` on the GUI thread
    // with the reply. False when another command is still running.
    bool runCommand(const std::string& cmd, int startQuietReads,
                    std::function<void(std::vector<std::string>, bool)> done);

    enum class Log { Connected, Lost, Health, Released, Polling, Sent, Ack, AckDiffers };
    void appendHistory(Log kind, const QString& text);

    void startPolling();
    void releasePort();

    // The device answers throughout a delivery, so checks keep running --
    // they are the only evidence the MCU is alive while output goes out.
    // This only makes a miss during one report louder.
    void setDelivering(bool delivering);

signals:
    // `text` is operator-facing: no port name, no round-trip figure.
    void linkStateChanged(bool healthy, const QString& text);
    // A miss while ultrasound is going out, which Abort cannot stop.
    void linkLostWhileDelivering(const QString& text);

private:
    void buildWidgets();
    void onLinkEvent(const beam::serialcom::LinkEvent& event);
    void setLamp(bool connected);
    void copyHistory(bool selectedOnly);

    QLabel* lamp_ = nullptr;
    QLabel* status_ = nullptr;
    QLabel* modelValue_ = nullptr;
    QLabel* serialValue_ = nullptr;
    QListWidget* history_ = nullptr;
    QPushButton* connectButton_ = nullptr;
    QPushButton* disconnectButton_ = nullptr;
    QPushButton* clearButton_ = nullptr;

    // Polled off the GUI thread: a scan with no device blocks for seconds.
    // Everything touching the monitor takes mutex_.
    std::unique_ptr<beam::serialcom::LinkMonitor> monitor_;
    mutable std::mutex mutex_;
    std::thread thread_;
    std::atomic<bool> stop_{false};
    // A foreground command runs on its own thread so the window keeps
    // painting while the device streams.
    std::thread commandThread_;
    std::atomic<bool> commandRunning_{false};
    bool delivering_ = false;

    bool healthy_ = false;
    QString lastDownMessage_;
};

}  // namespace beam::app
