#pragma once

#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace beam::serialcom {

enum class OpenFailure { InUse, NotFound, Other };

// Typed so a caller can report the reason without parsing OS text.
class SerialOpenError : public std::runtime_error {
public:
    SerialOpenError(const std::string& message, OpenFailure reason)
        : std::runtime_error(message), reason_(reason) {}
    OpenFailure reason() const { return reason_; }

private:
    OpenFailure reason_;
};

class SerialLink {
public:
    virtual ~SerialLink() = default;

    // Appends a single LF (0x0A) terminator.
    virtual void writeLine(const std::string& data) = 0;

    // Up to the next LF, without it; "" if the read times out first.
    virtual std::string readLine() = 0;

    virtual std::size_t bytesAvailable() = 0;
    virtual bool isOpen() const = 0;
};

// Opens on construction, closes on destruction. 115200 baud, 8N1, LF.
class SerialPort : public SerialLink {
public:
    explicit SerialPort(const std::string& portName, int baudRate = 115200, int readTimeoutMs = 1000);
    ~SerialPort() override;

    SerialPort(const SerialPort&) = delete;
    SerialPort& operator=(const SerialPort&) = delete;

    void writeLine(const std::string& data) override;
    std::string readLine() override;
    // Bytes as they arrive, terminator or not: tells silence from a missing LF.
    std::string readRaw(int windowMs);
    std::size_t bytesAvailable() override;
    bool isOpen() const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Ports that exist and can be opened right now.
std::vector<std::string> listAvailableComPorts();

// Every port the OS knows, including one another program is holding.
std::vector<std::string> listAllComPorts();

// USB identity behind a port; fields are empty when the OS does not say.
struct ComPortIdentity {
    std::string vendorId;
    std::string productId;
    std::string serial;
};
ComPortIdentity identifyComPort(const std::string& portName);

// Drains, then writes `<cmd>` plus a literal backslash and n, then LF. The
// firmware was written to that form.
bool sendSerialCommand(SerialLink& link, const std::string& cmd);

// Drains while more than one byte is buffered, so it can leave one behind.
void clearSerial(SerialLink& link);

// Sends `cmd` and collects the reply. `startQuietReads` is how many empty
// reads to tolerate before the first line; once lines are flowing a single
// empty read ends the burst, so a finished device is not waited on.
std::vector<std::string> sendAndCollect(SerialLink& link, const std::string& cmd,
                                        int maxLines = 100000, int startQuietReads = 1);

// PING; true when the reply starts with PONG.
bool isSerialHealthy(SerialLink& link);

}  // namespace beam::serialcom
