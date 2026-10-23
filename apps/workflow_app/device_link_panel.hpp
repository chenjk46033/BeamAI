#pragma once

// Owns the link to the device: the port, the polling thread, and the
// widgets that report it.

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

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

    enum class Log { Connected, Lost, Health, Released, Polling };
    void appendHistory(Log kind, const QString& text);

    void startPolling();
    void releasePort();

signals:
    // `text` is operator-facing: no port name, no round-trip figure.
    void linkStateChanged(bool healthy, const QString& text);

private:
    void buildWidgets();
    void onLinkEvent(const beam::serialcom::LinkEvent& event);
    void setLamp(bool connected);

    QLabel* lamp_ = nullptr;
    QLabel* status_ = nullptr;
    QListWidget* history_ = nullptr;
    QPushButton* connectButton_ = nullptr;
    QPushButton* disconnectButton_ = nullptr;

    // Polled off the GUI thread: a scan with no device blocks for seconds.
    // Everything touching the monitor takes mutex_.
    std::unique_ptr<beam::serialcom::LinkMonitor> monitor_;
    mutable std::mutex mutex_;
    std::thread thread_;
    std::atomic<bool> stop_{false};

    bool healthy_ = false;
    QString lastDownMessage_;
};

}  // namespace beam::app
