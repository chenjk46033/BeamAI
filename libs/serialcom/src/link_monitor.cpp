#include "serialcom/link_monitor.hpp"

#include <cctype>
#include <cstring>
#include <chrono>
#include <cstdio>
#include <thread>

namespace beam::serialcom {

namespace {

using Clock = std::chrono::steady_clock;

bool looksLikePong(const std::string& line) {
    // A BeamV0-form echo can put the literal "\n" in front of the reply.
    std::size_t at = 0;
    while (at < line.size() &&
           (line[at] == ' ' || line[at] == '\t' || line[at] == '\\' || line[at] == 'n'))
        ++at;
    if (at >= line.size() || line.size() - at < 4) at = 0;
    if (line.size() - at < 4) return false;
    static const char kPong[4] = {'P', 'O', 'N', 'G'};
    for (int i = 0; i < 4; ++i)
        if (std::toupper(static_cast<unsigned char>(line[at + static_cast<std::size_t>(i)])) !=
            kPong[i])
            return false;
    return true;
}

// Payload only: ping counts data, not framing.
std::size_t requestBytes(WireForm form) { return std::strlen(formPayload(form)); }

// "time<1ms" below a millisecond, "time=5ms" above.
const char* shortForm(WireForm form) { return form == WireForm::BeamV0 ? "BeamV0" : "plain"; }

std::string deviceLabel(const std::string& port) {
    const ComPortIdentity who = identifyComPort(port);
    if (who.vendorId.empty()) return port;
    return who.vendorId + ":" + who.productId + (who.serial.empty() ? "" : " " + who.serial);
}

std::string pingTime(double ms) {
    if (ms < 1.0) return "time<1ms";
    char buf[32];
    std::snprintf(buf, sizeof(buf), "time=%dms", static_cast<int>(ms + 0.5));
    return buf;
}

}  // namespace

const char* formName(WireForm form) {
    return form == WireForm::BeamV0 ? "BeamV0 (PING\\n + LF)" : "plain (PING + LF)";
}

const char* formPayload(WireForm form) { return form == WireForm::BeamV0 ? "PING\\n" : "PING"; }

const char* describe(OpenFailure reason) {
    switch (reason) {
        case OpenFailure::InUse: return "Destination port in use.";
        case OpenFailure::NotFound: return "Destination device not found.";
        case OpenFailure::Other: break;
    }
    return "General failure.";
}

const char* describe(LinkState state) {
    switch (state) {
        case LinkState::Searching: return "SCAN";
        case LinkState::Up: return "UP";
        case LinkState::Degraded: return "WARN";
        case LinkState::Down: break;
    }
    return "DOWN";
}

std::string visibleBytes(const std::string& text) {
    std::string out;
    for (const char c : text) {
        if (c >= 0x20 && c < 0x7f) {
            out.push_back(c);
        } else {
            char buf[8];
            std::snprintf(buf, sizeof(buf), "\\x%02x", static_cast<unsigned char>(c));
            out += buf;
        }
    }
    return out;
}

PingResult pingOnce(SerialLink& link, WireForm form) {
    PingResult result;
    try {
        if (!link.isOpen()) {
            result.error = "port is closed";
            return result;
        }
        clearSerial(link);
        const auto sentAt = Clock::now();
        link.writeLine(formPayload(form));
        for (;;) {
            const std::string line = link.readLine();
            if (line.empty()) return result;  // the link's own read timeout expired
            if (looksLikePong(line)) {
                result.pong = true;
                result.rttMs =
                    std::chrono::duration<double, std::milli>(Clock::now() - sentAt).count();
                result.reply = line;
                return result;
            }
            if (result.reply.empty()) result.reply = line;
        }
    } catch (const std::exception& e) {
        result.error = e.what();
        return result;
    }
}

LinkMonitor::LinkMonitor(Config config) : config_(std::move(config)) {
    if (!config_.listPorts) config_.listPorts = [] { return listAllComPorts(); };  // a busy port is the one to report
    if (!config_.openPort) {
        config_.openPort = [](const std::string& port, int baud,
                              int timeoutMs) -> std::unique_ptr<SerialLink> {
            return std::make_unique<SerialPort>(port, baud, timeoutMs);
        };
    }
    if (!config_.sleep) {
        config_.sleep = [](int ms) {
            std::this_thread::sleep_for(std::chrono::milliseconds(ms));
        };
    }
    form_ = config_.form;
}

LinkMonitor::~LinkMonitor() = default;

void LinkMonitor::setObserver(std::function<void(const LinkEvent&)> observer) {
    observer_ = std::move(observer);
}

void LinkMonitor::report(LinkState state, const std::string& message, double rttMs, bool pong) {
    state_ = state;
    if (!observer_) return;
    LinkEvent event;
    event.state = state;
    event.message = message;
    event.port = portName_;
    event.rttMs = rttMs;
    event.pong = pong;
    observer_(event);
}

bool LinkMonitor::probe(const std::string& name) {
    const std::string label = deviceLabel(name);
    report(LinkState::Searching,
           "Pinging " + label + " with " + std::to_string(requestBytes(config_.form)) +
               " bytes of data:");
    std::unique_ptr<SerialLink> port;
    try {
        port = config_.openPort(name, config_.baudRate, config_.replyTimeoutMs);
    } catch (const SerialOpenError& e) {
        report(LinkState::Searching, "Reply from " + label + ": " + describe(e.reason()));
        return false;
    } catch (const std::exception&) {
        report(LinkState::Searching, "Reply from " + label + ": General failure.");
        return false;
    }
    if (port == nullptr) {
        report(LinkState::Searching, "Reply from " + label + ": General failure.");
        return false;
    }
    config_.sleep(config_.settleMs);

    std::vector<WireForm> forms;
    if (config_.autoDetectForm) {
        forms = {WireForm::BeamV0, WireForm::Plain};
    } else {
        forms = {config_.form};
    }
    for (const WireForm form : forms) {
        // Two tries: the first can land while the MCU is still booting.
        for (int attempt = 0; attempt < 2; ++attempt) {
            const PingResult result = pingOnce(*port, form);
            if (result.pong) {
                link_ = std::move(port);
                portName_ = name;
                form_ = form;
                lastRttMs_ = result.rttMs;
                lastReplyBytes_ = result.reply.size();
                consecutiveMisses_ = 0;
                const ComPortIdentity who = identifyComPort(name);
                deviceId_ = who.serial;
                if (!who.vendorId.empty())
                    deviceId_ = who.vendorId + ":" + who.productId +
                                (who.serial.empty() ? "" : " " + who.serial);
                return true;
            }
            if (!result.error.empty()) {
                report(LinkState::Searching, "Reply from " + label + ": General failure.");
                return false;
            }
        }
        report(LinkState::Searching, "Request timed out.");
    }
    return false;
}

bool LinkMonitor::findDevice() {
    if (!config_.forcedPort.empty()) {
        return probe(config_.forcedPort);
    }
    const std::vector<std::string> ports = config_.listPorts();
    if (ports.empty()) {
        report(LinkState::Searching, "No serial ports available.");
        return false;
    }
    for (const std::string& name : ports)
        if (probe(name)) return true;
    return false;
}

std::string LinkMonitor::replyLine(double rttMs) const {
    return "Reply from " + portName_ +
           (deviceId_.empty() ? "" : " [deviceID " + deviceId_ + "]") +
           ": bytes=" + std::to_string(lastReplyBytes_) + " " + pingTime(rttMs) + " form=" +
           shortForm(form_);
}

void LinkMonitor::step() {
    if (link_ == nullptr) {
        if (findDevice()) {
            scanFailed_ = false;
            const std::string who =
                deviceId_.empty() ? portName_ : "deviceID " + deviceId_;
            report(LinkState::Up, who, lastRttMs_, true);
        } else {
            scanFailed_ = true;
            portName_.clear();
            report(LinkState::Down, "Request timed out.");
        }
        return;
    }

    const PingResult result = pingOnce(*link_, form_);
    ++stats_.sent;
    if (result.pong) {
        ++stats_.pongs;
        consecutiveMisses_ = 0;
        lastRttMs_ = result.rttMs;
        lastReplyBytes_ = result.reply.size();
        report(LinkState::Up, replyLine(result.rttMs), result.rttMs, true);
        return;
    }

    ++stats_.misses;
    ++consecutiveMisses_;
    const std::string label = deviceId_.empty() ? portName_ : deviceId_;
    std::string why;
    if (!result.error.empty()) {
        why = "Reply from " + label + ": General failure.";
    } else if (!result.reply.empty()) {
        why = "Reply from " + label + ": " + visibleBytes(result.reply);
    } else {
        why = "Request timed out.";
    }
    report(LinkState::Degraded, why);

    if (consecutiveMisses_ >= config_.failuresBeforeRescan) {
        ++stats_.reconnects;
        const std::string dropped = portName_;
        link_.reset();
        portName_.clear();
        deviceId_.clear();
        consecutiveMisses_ = 0;
        scanFailed_ = true;
        report(LinkState::Down, "Lost connection to " + dropped + ".");
    }
}

int LinkMonitor::sleepHintMs() const {
    if (link_ == nullptr) return scanFailed_ ? config_.rescanDelayMs : 0;
    return config_.pingIntervalMs;
}

void LinkMonitor::disconnect(const std::string& why) {
    if (link_ == nullptr) return;
    const std::string dropped = portName_;
    link_.reset();
    portName_.clear();
    consecutiveMisses_ = 0;
    report(LinkState::Down, why.empty() ? ("disconnected " + dropped) : why);
}

void LinkMonitor::setForcedPort(const std::string& port) {
    if (config_.forcedPort == port) return;
    config_.forcedPort = port;
    disconnect(port.empty() ? "port set to auto-detect" : ("port set to " + port));
    scanFailed_ = false;
}

}  // namespace beam::serialcom
