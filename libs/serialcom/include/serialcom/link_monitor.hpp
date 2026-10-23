#pragma once

// Finds the device, PING/PONGs it, and reconnects after a drop. One step()
// per tick, so a console loop and a timer can both drive it.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "serialcom/serial_port.hpp"

namespace beam::serialcom {

// Which bytes "PING" goes out as: a literal backslash and n before the LF,
// or neither. The monitor tries both and reports which one answered.
enum class WireForm { BeamV0, Plain };

const char* formName(WireForm form);
const char* formPayload(WireForm form);

enum class LinkState {
    Searching,  // scanning ports, nothing answering yet
    Up,         // last ping answered
    Degraded,   // connected, but one or more pings missed
    Down,       // no link
};

const char* describe(LinkState state);
const char* describe(OpenFailure reason);

struct LinkEvent {
    LinkState state = LinkState::Down;
    std::string message;  // already formatted for a reader
    std::string port;     // empty while searching
    double rttMs = 0.0;
    bool pong = false;
};

struct PingResult {
    bool pong = false;
    double rttMs = 0.0;
    std::string reply;  // first line received, PONG or not
    std::string error;  // set when the link itself threw
};

// Waits for the PONG until the link's own read timeout expires.
PingResult pingOnce(SerialLink& link, WireForm form);

// Control characters escaped, so a mismatch is readable.
std::string visibleBytes(const std::string& text);

class LinkMonitor {
public:
    struct Config {
        std::string forcedPort;  // empty: auto-detect
        int baudRate = 115200;
        int replyTimeoutMs = 700;
        int pingIntervalMs = 1000;
        // Opening a port asserts DTR, which resets the MCU; let it boot.
        int settleMs = 400;
        int failuresBeforeRescan = 3;
        int rescanDelayMs = 2000;
        bool autoDetectForm = true;
        WireForm form = WireForm::BeamV0;

        // Seams for tests; the defaults are the real OS-backed ones.
        std::function<std::vector<std::string>()> listPorts;
        std::function<std::unique_ptr<SerialLink>(const std::string& port, int baud, int timeoutMs)>
            openPort;
        std::function<void(int ms)> sleep;
    };

    struct Stats {
        long long sent = 0;
        long long pongs = 0;
        long long misses = 0;
        long long reconnects = 0;
    };

    explicit LinkMonitor(Config config);
    ~LinkMonitor();

    LinkMonitor(const LinkMonitor&) = delete;
    LinkMonitor& operator=(const LinkMonitor&) = delete;

    void setObserver(std::function<void(const LinkEvent&)> observer);

    // A scan when there is no link, otherwise one ping.
    void step();

    // How long to wait before the next step().
    int sleepHintMs() const;

    LinkState state() const { return state_; }
    const std::string& portName() const { return portName_; }
    WireForm form() const { return form_; }
    // The board's USB serial, empty when the OS does not say.
    const std::string& deviceId() const { return deviceId_; }
    double lastRttMs() const { return lastRttMs_; }
    int consecutiveMisses() const { return consecutiveMisses_; }
    Stats stats() const { return stats_; }

    // The open port, for sending real commands. Null while the link is down.
    SerialLink* link() { return link_.get(); }

    void disconnect(const std::string& why);

    // Empty returns to auto-detect.
    void setForcedPort(const std::string& port);

private:
    std::string replyLine(double rttMs) const;
    void report(LinkState state, const std::string& message, double rttMs = 0.0, bool pong = false);
    bool probe(const std::string& name);
    bool findDevice();

    Config config_;
    std::function<void(const LinkEvent&)> observer_;
    std::unique_ptr<SerialLink> link_;
    std::string portName_;
    WireForm form_ = WireForm::BeamV0;
    LinkState state_ = LinkState::Down;
    double lastRttMs_ = 0.0;
    std::size_t lastReplyBytes_ = 0;
    std::string deviceId_;
    int consecutiveMisses_ = 0;
    bool scanFailed_ = false;
    Stats stats_;
};

}  // namespace beam::serialcom
