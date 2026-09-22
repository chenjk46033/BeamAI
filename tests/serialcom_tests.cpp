#include <cmath>
#include <deque>
#include <stdexcept>
#include <string>

#include <gtest/gtest.h>

#include "serialcom/command.hpp"
#include "serialcom/serial_port.hpp"

using namespace beam::serialcom;

namespace {

// In-memory SerialLink for exercising the ported command flow without a
// device. `inbound` is what the "firmware" has queued for us to read;
// `written` accumulates every byte writeLine sent.
class FakeSerialLink : public SerialLink {
public:
    std::deque<std::string> inbound;  // pending lines (no trailing LF)
    std::string written;
    bool open = true;
    // If set, a writeLine whose data begins with "PING" queues this reply,
    // modelling firmware that answers *after* the command is sent (so the
    // reply survives the clearSerial drain sendSerialCommand does first).
    std::string pingReply;

    void writeLine(const std::string& data) override {
        if (!open) {
            throw std::runtime_error("FakeSerialLink: closed");
        }
        written += data;
        written.push_back('\n');
        if (!pingReply.empty() && data.rfind("PING", 0) == 0) {
            inbound.push_back(pingReply);
        }
    }
    std::string readLine() override {
        if (inbound.empty()) {
            return std::string();
        }
        const std::string line = inbound.front();
        inbound.pop_front();
        return line;
    }
    std::size_t bytesAvailable() override {
        std::size_t n = 0;
        for (const std::string& s : inbound) {
            n += s.size() + 1;  // + LF
        }
        return n;
    }
    bool isOpen() const override { return open; }
};

}  // namespace

TEST(SetSerialCommandFromStimParams, BuildsCommandString) {
    SonicationTiming t;
    t.pd = 0.001;   // -> 1 ms
    t.pi = 0.002;   // -> 2 ms
    t.bd = 0.010;   // -> 10 ms
    t.bi = 0.100;   // -> 100 ms
    t.startTime = 0.0;
    t.endTime = 5.0;  // -> D5

    EXPECT_EQ(setSerialCommandFromStimParams(t, 0.65), "Stim_PR0.65_PD1_PI2_BD10_BI100_D5");
}

TEST(ParseCorrectionReceiveString, ParsesFiveFields) {
    const CorrectionReceive r = parseCorrectionReceiveString("1.5,2.5,3.5,4.5,5.5");
    EXPECT_DOUBLE_EQ(r.ch0Max, 1.5);
    EXPECT_DOUBLE_EQ(r.ch1Max, 2.5);
    EXPECT_DOUBLE_EQ(r.rawValue, 3.5);
    EXPECT_DOUBLE_EQ(r.attenuation, 4.5);
    EXPECT_DOUBLE_EQ(r.calibrationVal, 5.5);
}

TEST(ParseCorrectionReceiveString, UnparseableFieldIsNaN) {
    const CorrectionReceive r = parseCorrectionReceiveString("10,20,-3,notanumber,5");
    EXPECT_DOUBLE_EQ(r.ch0Max, 10.0);
    EXPECT_DOUBLE_EQ(r.rawValue, -3.0);
    EXPECT_TRUE(std::isnan(r.attenuation));
    EXPECT_DOUBLE_EQ(r.calibrationVal, 5.0);
}

TEST(ParseCorrectionReceiveString, TooFewFieldsThrows) {
    EXPECT_THROW(parseCorrectionReceiveString("1,2,3"), std::runtime_error);
}

// --- serial transport command flow ---

TEST(SendSerialCommand, WritesCmdWithLiteralBackslashNThenLf) {
    FakeSerialLink link;
    EXPECT_TRUE(sendSerialCommand(link, "Stim_PR0.5_D5"));
    // sendSerialCommand.m: writeline(serial, [cmd,'\n']) -- '\n' is the two
    // chars '\' 'n' -- then writeline adds the LF terminator.
    EXPECT_EQ(link.written, "Stim_PR0.5_D5\\n\n");
}

TEST(SendSerialCommand, ClosedLinkWritesNothingAndReturnsFalse) {
    FakeSerialLink link;
    link.open = false;
    EXPECT_FALSE(sendSerialCommand(link, "PING"));
    EXPECT_TRUE(link.written.empty());
}

TEST(SendSerialCommand, DrainsPendingInputFirst) {
    FakeSerialLink link;
    link.inbound = {"stale1", "stale2", "stale3"};
    sendSerialCommand(link, "GO");
    // clearSerial drains while > 1 byte available; a short tail may remain
    // but the bulk is gone.
    EXPECT_LE(link.inbound.size(), 1u);
}

TEST(ClearSerial, DrainsAllQueuedLines) {
    FakeSerialLink link;
    link.inbound = {"a", "b"};
    clearSerial(link);
    EXPECT_TRUE(link.inbound.empty());
}

TEST(IsSerialHealthy, TrueOnPongCaseInsensitive) {
    FakeSerialLink link;
    link.pingReply = "pong-v2";
    EXPECT_TRUE(isSerialHealthy(link));
}

TEST(IsSerialHealthy, FalseOnWrongReplyOrClosed) {
    FakeSerialLink bad;
    bad.pingReply = "NOPE";
    EXPECT_FALSE(isSerialHealthy(bad));

    FakeSerialLink closed;
    closed.open = false;
    EXPECT_FALSE(isSerialHealthy(closed));

    FakeSerialLink silent;  // no reply queued -> readLine() returns ""
    EXPECT_FALSE(isSerialHealthy(silent));
}

TEST(ListAvailableComPorts, DoesNotThrow) {
    // Can't assert contents in CI; just exercise the platform code path.
    EXPECT_NO_THROW((void)listAvailableComPorts());
}

TEST(SerialPort, OpeningAMissingPortThrows) {
    EXPECT_THROW(SerialPort("NOPE_NOT_A_PORT_XYZ"), std::runtime_error);
}
