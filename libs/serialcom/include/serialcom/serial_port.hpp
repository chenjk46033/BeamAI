#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

// Cross-platform serial transport for Beam's ultrasound driver link, plus
// the ported command flow from BeamV0/GUIMatlab/BEAM/SerialCom/
// (connectSerial / sendSerialCommand / clearSerial / isSerialHealthy /
// listAvailableCOMPorts), with the `app`-widget lines
// (ConnectedLamp / uialert / CouplingStatusTextArea) dropped.
//
// Beam talks to its hardware over USB/serial -- this is the hardware layer.
// It is NOT Verasonics (see docs/known_gaps_stimulation.md).

namespace beam::serialcom {

// Transport abstraction, so the ported command flow is testable without a
// real device (tests use an in-memory fake). SerialPort is the OS-backed
// implementation.
class SerialLink {
public:
    virtual ~SerialLink() = default;

    // Writes `data` followed by a single LF (0x0A) terminator -- MATLAB
    // `configureTerminator(s,"LF")` + `writeline`.
    virtual void writeLine(const std::string& data) = 0;

    // Reads bytes up to and including the next LF; returns the line without
    // the trailing LF (and without a trailing CR, if present). Returns ""
    // if the read times out before a full line arrives -- the caller sees
    // the same "no reply" it would from a timed-out MATLAB `readline`.
    virtual std::string readLine() = 0;

    // Bytes buffered and readable without blocking -- MATLAB
    // `NumBytesAvailable`.
    virtual std::size_t bytesAvailable() = 0;

    virtual bool isOpen() const = 0;
};

// OS-backed serial port. Opens on construction (throws std::runtime_error
// on failure, like `serialport()` erroring), closes on destruction.
// Defaults match connectSerial.m: 115200 baud, 8N1, LF terminator; read
// timeout is this layer's own (MATLAB leaves it at the 10 s default).
class SerialPort : public SerialLink {
public:
    explicit SerialPort(const std::string& portName, int baudRate = 115200, int readTimeoutMs = 1000);
    ~SerialPort() override;

    SerialPort(const SerialPort&) = delete;
    SerialPort& operator=(const SerialPort&) = delete;

    void writeLine(const std::string& data) override;
    std::string readLine() override;
    std::size_t bytesAvailable() override;
    bool isOpen() const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Port of listAvailableCOMPorts.m / `serialportlist("available")`: names of
// serial ports that exist and are not currently open. Best-effort per
// platform. Returns an empty vector when there are none -- the MATLAB
// source's `{'No COM ports'}` placeholder is a UI concern, left to the
// caller.
std::vector<std::string> listAvailableComPorts();

// --- ported command flow (SerialCom/*.m) ---

// Port of sendSerialCommand.m: drains pending input (clearSerial), then
// writes the command. Returns false and writes nothing if the link is
// closed -- matching `~isempty(app.serial) && isvalid(app.serial)` and the
// `rcv = 0` default.
//
// Faithful-port quirk: the source writes `[cmd,'\n']`, and `'\n'` in a
// single-quoted MATLAB string is the two literal characters '\' and 'n',
// not a newline; `writeline` then appends the real LF. So the bytes on the
// wire are `<cmd>\nLF`. Reproduced as-is (the STM32 firmware is written to
// this contract).
bool sendSerialCommand(SerialLink& link, const std::string& cmd);

// Port of clearSerial.m: `readLine()` while `bytesAvailable() > 1`. The
// source's `> 1` (not `> 0`) is kept -- it can leave a trailing byte.
void clearSerial(SerialLink& link);

// Port of isSerialHealthy.m: sendSerialCommand("PING"), read one line, and
// check it begins with "PONG" (case-insensitive, first 4 chars). A closed
// link or any I/O error -> false.
bool isSerialHealthy(SerialLink& link);

}  // namespace beam::serialcom
