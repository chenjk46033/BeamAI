#include "device_link_panel.hpp"

#include <algorithm>
#include <chrono>

#include <QColor>
#include <QAction>
#include <QClipboard>
#include <QGuiApplication>
#include <QKeySequence>
#include <QMenu>
#include <QShortcut>
#include <QStringList>
#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QVBoxLayout>

#include "serialcom/link_monitor.hpp"
#include "serialcom/serial_port.hpp"

namespace beam::app {

namespace {


constexpr int kPollIntervalMs = 3000;
constexpr int kStopSliceMs = 25;
constexpr int kHistoryLimit = 1000;

constexpr char kHistoryStyle[] =
    "QListWidget { color: #dcebef; background: #183944; border: 1px solid #2b6172; "
    "border-radius: 5px; padding: 4px; font-family: Consolas, 'Courier New', monospace; "
    "font-size: 11px; } "
    "QListWidget::item { padding: 1px 2px; }";

constexpr char kButtonStyle[] =
    "QPushButton { color: #eafaff; background: #24505f; border: 1px solid #41788a; "
    "border-radius: 4px; padding: 4px 14px; } "
    "QPushButton:hover:enabled { background: #2d6477; } "
    "QPushButton:disabled { color: #7d949c; background: #1b3a45; border-color: #2b5260; }";

constexpr char kClearStyle[] =
    "QPushButton { color: #eafaff; background: #2d6477; border: 1px solid #6ba7ba; "
    "border-radius: 3px; padding: 1px 10px; font-size: 10px; } "
    "QPushButton:hover { background: #3f8198; border-color: #9fd6e5; }";

constexpr char kLampOn[] =
    "QLabel { background: #3fbf6f; border: 1px solid #2c8a50; border-radius: 8px; }";
constexpr char kLampOff[] =
    "QLabel { background: #b3262a; border: 1px solid #7d1a1d; border-radius: 8px; }";

const char* label(DeviceLinkPanel::Log kind) {
    switch (kind) {
        case DeviceLinkPanel::Log::Connected: return "CONNECTED";
        case DeviceLinkPanel::Log::Lost: return "LOST";
        case DeviceLinkPanel::Log::Released: return "DISCONNECTED";
        case DeviceLinkPanel::Log::Polling: return "POLLING";
        case DeviceLinkPanel::Log::Sent: return "SENT";
        case DeviceLinkPanel::Log::Health: break;
    }
    return "";
}

// Light on the panel's dark ground; the first set was picked for a white one.
QColor colour(DeviceLinkPanel::Log kind) {
    switch (kind) {
        case DeviceLinkPanel::Log::Connected: return QColor(0x6e, 0xe7, 0xa0);
        case DeviceLinkPanel::Log::Lost: return QColor(0xff, 0x82, 0x90);
        case DeviceLinkPanel::Log::Sent: return QColor(0xe0, 0xa0, 0x3f);
        default: break;
    }
    return QColor(0xcf, 0xe3, 0xe9);
}

}  // namespace

DeviceLinkPanel::DeviceLinkPanel(QWidget* parent) : QWidget(parent) {
    buildWidgets();
    setLamp(false);
    startPolling();
}

DeviceLinkPanel::~DeviceLinkPanel() {
    stop_.store(true);
    if (thread_.joinable()) thread_.join();
    if (commandThread_.joinable()) commandThread_.join();
}

void DeviceLinkPanel::buildWidgets() {
    lamp_ = new QLabel(this);
    lamp_->setFixedSize(16, 16);
    lamp_->setAutoFillBackground(true);

    status_ = new QLabel(QStringLiteral("Searching for device..."), this);
    status_->setStyleSheet(QStringLiteral("QLabel { color: #eafaff; font-size: 14px; font-weight: 600; }"));

    connectButton_ = new QPushButton(QStringLiteral("Connect"), this);
    connectButton_->setToolTip(QStringLiteral("Find the device and start checking it."));
    disconnectButton_ = new QPushButton(QStringLiteral("Disconnect"), this);
    disconnectButton_->setToolTip(
        QStringLiteral("Close the port so another program can use it."));
    clearButton_ = new QPushButton(QStringLiteral("Clear"), this);
    clearButton_->setToolTip(QStringLiteral("Empty the list. The link is not touched."));
    clearButton_->setCursor(Qt::PointingHandCursor);
    clearButton_->setStyleSheet(QString::fromLatin1(kClearStyle));
    connect(clearButton_, &QPushButton::clicked, this, [this] { history_->clear(); });

    for (QPushButton* button : {connectButton_, disconnectButton_, clearButton_}) {
        button->setMinimumHeight(28);
        button->setStyleSheet(QString::fromLatin1(kButtonStyle));
    }
    connect(connectButton_, &QPushButton::clicked, this, [this] { startPolling(); });
    connect(disconnectButton_, &QPushButton::clicked, this, [this] { releasePort(); });

    history_ = new QListWidget(this);
    history_->setStyleSheet(QString::fromLatin1(kHistoryStyle));
    history_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    history_->setUniformItemSizes(true);
    // So a line can be selected and pasted into a bug report.
    history_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    history_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(history_, &QListWidget::customContextMenuRequested, this, [this](const QPoint& at) {
        QMenu menu(history_);
        QAction* copySelected = menu.addAction(QStringLiteral("Copy selected"));
        QAction* copyAll = menu.addAction(QStringLiteral("Copy all"));
        menu.addSeparator();
        QAction* clear = menu.addAction(QStringLiteral("Clear"));
        const QAction* chosen = menu.exec(history_->viewport()->mapToGlobal(at));
        if (chosen == copySelected) copyHistory(true);
        else if (chosen == copyAll) copyHistory(false);
        else if (chosen == clear) history_->clear();
    });
    auto* copyShortcut = new QShortcut(QKeySequence::Copy, history_);
    copyShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(copyShortcut, &QShortcut::activated, this, [this] { copyHistory(true); });
    history_->setFixedHeight(112);

    auto* heading = new QLabel(QStringLiteral("Device link"), this);
    heading->setStyleSheet(QStringLiteral("QLabel { color: #f2f7f8; font-size: 20px; font-weight: 700; }"));

    auto* row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 4);
    row->setSpacing(8);
    // Left, not right: the page is wider than the screen, so anything
    // pushed to its right edge is off the display.
    row->addWidget(lamp_);
    row->addWidget(status_);
    row->addSpacing(16);
    row->addWidget(connectButton_);
    row->addWidget(disconnectButton_);
    row->addWidget(clearButton_);
    row->addStretch(1);

    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    column->addWidget(heading);
    column->addLayout(row);
    column->addWidget(history_);
}

// Selected lines, or all of them when nothing is selected.
void DeviceLinkPanel::copyHistory(bool selectedOnly) {
    QStringList lines;
    const QList<QListWidgetItem*> chosen = history_->selectedItems();
    if (selectedOnly && !chosen.isEmpty()) {
        for (int row = 0; row < history_->count(); ++row)
            if (history_->item(row)->isSelected()) lines << history_->item(row)->text();
    } else {
        for (int row = 0; row < history_->count(); ++row) lines << history_->item(row)->text();
    }
    QGuiApplication::clipboard()->setText(lines.join(QChar('\n')));
}

void DeviceLinkPanel::setLamp(bool connected) {
    lamp_->setStyleSheet(QString::fromLatin1(connected ? kLampOn : kLampOff));
}

void DeviceLinkPanel::appendHistory(Log kind, const QString& text) {
    const QString stamp =
        QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
    const QString tag = QString::fromLatin1(label(kind));
    auto* item = new QListWidgetItem(
        tag.isEmpty() ? QStringLiteral("%1  %2").arg(stamp, text)
                      : QStringLiteral("%1  %2  %3").arg(stamp, tag, text));
    item->setForeground(colour(kind));
    history_->addItem(item);
    while (history_->count() > kHistoryLimit) delete history_->takeItem(0);
    history_->scrollToItem(item);
}

void DeviceLinkPanel::onLinkEvent(const beam::serialcom::LinkEvent& event) {
    // A queued event can land after releasePort(); honouring it would undo it.
    if (monitor_ == nullptr) return;

    const bool up = event.state == beam::serialcom::LinkState::Up;
    const bool searching = event.state == beam::serialcom::LinkState::Searching;
    const QString message = QString::fromStdString(event.message);
    // No port name here: the history carries it, an operator does not want it.
    const QString plain = up         ? QStringLiteral("Device connected")
                          : searching ? QStringLiteral("Searching for device...")
                                      : QStringLiteral("Device not found");

    if (up && !healthy_) {
        appendHistory(Log::Connected, message);
        lastDownMessage_.clear();
    } else if (!up && healthy_) {
        appendHistory(Log::Lost, message);
        lastDownMessage_ = message;
        if (delivering_) emit linkLostWhileDelivering(message);
    } else if (up) {
        appendHistory(Log::Health, message);
    } else if (event.state == beam::serialcom::LinkState::Down && message != lastDownMessage_) {
        // Only a changed reason earns a line; a repeating one fills the list.
        appendHistory(Log::Health, message);
        lastDownMessage_ = message;
    }

    healthy_ = up;
    status_->setText(plain);
    setLamp(up);
    emit linkStateChanged(up, plain);
}

void DeviceLinkPanel::startPolling() {
    if (monitor_ != nullptr) return;
    appendHistory(Log::Polling, QStringLiteral("searching for device"));

    beam::serialcom::LinkMonitor::Config config;
    config.pingIntervalMs = kPollIntervalMs;
    monitor_ = std::make_unique<beam::serialcom::LinkMonitor>(std::move(config));
    monitor_->setObserver([this](const beam::serialcom::LinkEvent& event) {
        // Hop to the GUI thread; the monitor calls this from its own.
        QMetaObject::invokeMethod(
            this, [this, event] { onLinkEvent(event); }, Qt::QueuedConnection);
    });

    stop_.store(false);
    thread_ = std::thread([this] {
        while (!stop_.load()) {
            int wait = 0;
            {
                std::lock_guard<std::mutex> guard(mutex_);
                if (monitor_ == nullptr) return;
                monitor_->step();
                wait = monitor_->sleepHintMs();
            }
            // In slices, so stopping does not wait out a whole interval.
            for (int waited = 0; waited < std::max(wait, kStopSliceMs) && !stop_.load();
                 waited += kStopSliceMs)
                std::this_thread::sleep_for(std::chrono::milliseconds(kStopSliceMs));
        }
    });
}

void DeviceLinkPanel::releasePort() {
    if (monitor_ == nullptr) return;  // already released; the button stays live
    stop_.store(true);
    if (thread_.joinable()) thread_.join();
    {
        std::lock_guard<std::mutex> guard(mutex_);
        monitor_.reset();  // closes the port
    }
    healthy_ = false;
    lastDownMessage_.clear();
    setLamp(false);
    const QString plain = QStringLiteral("Disconnected");
    status_->setText(plain);
    appendHistory(Log::Released, QStringLiteral("port closed, checks stopped"));
    emit linkStateChanged(false, plain);
}

bool DeviceLinkPanel::runCommand(const std::string& cmd, int startQuietReads,
                                 std::function<void(std::vector<std::string>, bool)> done) {
    if (commandRunning_.exchange(true)) return false;
    appendHistory(Log::Sent, QString::fromStdString(cmd));
    if (commandThread_.joinable()) commandThread_.join();
    commandThread_ = std::thread([this, cmd, startQuietReads, done = std::move(done)] {
        std::vector<std::string> lines;
        bool ok = false;
        const auto started = std::chrono::steady_clock::now();
        try {
            // The lock keeps a health check from starting one of its own
            // between this command and its reply.
            ok = withLink([&](beam::serialcom::SerialLink& link) {
                lines = beam::serialcom::sendAndCollect(link, cmd, 100000, startQuietReads);
            });
            if (!ok) {
                const auto ports = beam::serialcom::listAvailableComPorts();
                if (!ports.empty()) {
                    beam::serialcom::SerialPort own(ports.front());
                    lines = beam::serialcom::sendAndCollect(own, cmd, 100000, startQuietReads);
                    ok = true;
                }
            }
        } catch (const std::exception&) {
            ok = false;
        }
        const double ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started)
                .count();
        commandRunning_.store(false);
        QMetaObject::invokeMethod(
            this,
            [this, done, lines, ok, cmd, ms] {
                appendHistory(Log::Health, QStringLiteral("Reply to %1: lines=%2 time=%3ms")
                                               .arg(QString::fromStdString(cmd))
                                               .arg(lines.size())
                                               .arg(static_cast<int>(ms)));
                done(lines, ok);
            },
            Qt::QueuedConnection);
    });
    return true;
}

void DeviceLinkPanel::setDelivering(bool delivering) { delivering_ = delivering; }

bool DeviceLinkPanel::askDevice() {
    std::lock_guard<std::mutex> guard(mutex_);
    if (monitor_ == nullptr) return false;
    beam::serialcom::SerialLink* link = monitor_->link();
    if (link == nullptr) return false;
    return beam::serialcom::pingOnce(*link, monitor_->form()).pong;
}

bool DeviceLinkPanel::withLink(const std::function<void(beam::serialcom::SerialLink&)>& use) {
    std::lock_guard<std::mutex> guard(mutex_);
    if (monitor_ == nullptr) return false;
    beam::serialcom::SerialLink* link = monitor_->link();
    if (link == nullptr) return false;
    use(*link);
    return true;
}

}  // namespace beam::app
