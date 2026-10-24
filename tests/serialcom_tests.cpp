#include <cmath>
#include <deque>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "serialcom/command.hpp"
#include "serialcom/link_monitor.hpp"
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

// --- LinkMonitor: auto-detect, wire-form discovery, reconnection ---

namespace {

// A device on one port. `speaks` is the only wire form it answers, so a test
// can tell BeamV0 firmware from plain-line firmware.
class ScriptedDevice : public SerialLink {
public:
    ScriptedDevice(WireForm speaks, int goodPings)
        : speaks_(speaks), remaining_(goodPings) {}

    void writeLine(const std::string& data) override {
        if (!open_) throw std::runtime_error("ScriptedDevice: closed");
        const bool matches = data == formPayload(speaks_);
        if (matches && remaining_ != 0) {
            if (remaining_ > 0) --remaining_;
            inbound_.push_back("PONG");
        }
    }
    std::string readLine() override {
        if (inbound_.empty()) return std::string();
        const std::string line = inbound_.front();
        inbound_.pop_front();
        return line;
    }
    std::size_t bytesAvailable() override {
        std::size_t n = 0;
        for (const std::string& s : inbound_) n += s.size() + 1;
        return n;
    }
    bool isOpen() const override { return open_; }

    void unplug() { open_ = false; }

private:
    WireForm speaks_;
    int remaining_;  // -1 = forever
    std::deque<std::string> inbound_;
    bool open_ = true;
};

// Answers every command with a banner, never with PONG.
class NoisyDevice : public SerialLink {
public:
    void writeLine(const std::string&) override { inbound_.push_back("BOOT v1.2"); }
    std::string readLine() override {
        if (inbound_.empty()) return std::string();
        const std::string line = inbound_.front();
        inbound_.pop_front();
        return line;
    }
    std::size_t bytesAvailable() override {
        std::size_t n = 0;
        for (const std::string& s : inbound_) n += s.size() + 1;
        return n;
    }
    bool isOpen() const override { return true; }

private:
    std::deque<std::string> inbound_;
};

// Nothing on this port: opens fine, never answers.
class DeadPort : public SerialLink {
public:
    void writeLine(const std::string&) override {}
    std::string readLine() override { return std::string(); }
    std::size_t bytesAvailable() override { return 0; }
    bool isOpen() const override { return true; }
};

LinkMonitor::Config testConfig() {
    LinkMonitor::Config config;
    config.settleMs = 0;
    config.sleep = [](int) {};
    return config;
}

}  // namespace

TEST(LinkMonitor, AutoDetectSkipsSilentPortsAndKeepsTheOneThatAnswers) {
    auto config = testConfig();
    config.listPorts = [] { return std::vector<std::string>{"COM3", "COM7", "COM9"}; };
    config.openPort = [](const std::string& port, int, int) -> std::unique_ptr<SerialLink> {
        if (port == "COM7") return std::make_unique<ScriptedDevice>(WireForm::BeamV0, -1);
        return std::make_unique<DeadPort>();
    };
    LinkMonitor monitor(std::move(config));

    monitor.step();
    EXPECT_EQ(monitor.state(), LinkState::Up);
    EXPECT_EQ(monitor.portName(), "COM7");
}

TEST(LinkMonitor, DiscoversWhichWireFormTheFirmwareSpeaks) {
    for (const WireForm speaks : {WireForm::BeamV0, WireForm::Plain}) {
        auto config = testConfig();
        config.listPorts = [] { return std::vector<std::string>{"COM4"}; };
        config.openPort = [speaks](const std::string&, int, int) -> std::unique_ptr<SerialLink> {
            return std::make_unique<ScriptedDevice>(speaks, -1);
        };
        LinkMonitor monitor(std::move(config));
        monitor.step();
        EXPECT_EQ(monitor.state(), LinkState::Up);
        EXPECT_EQ(monitor.form(), speaks);
    }
}

TEST(LinkMonitor, ForcedPortIsUsedWithoutScanning) {
    bool scanned = false;
    auto config = testConfig();
    config.forcedPort = "COM12";
    config.listPorts = [&scanned] {
        scanned = true;
        return std::vector<std::string>{"COM1"};
    };
    config.openPort = [](const std::string& port, int, int) -> std::unique_ptr<SerialLink> {
        EXPECT_EQ(port, "COM12");
        return std::make_unique<ScriptedDevice>(WireForm::BeamV0, -1);
    };
    LinkMonitor monitor(std::move(config));

    monitor.step();
    EXPECT_EQ(monitor.portName(), "COM12");
    EXPECT_FALSE(scanned);
}

TEST(LinkMonitor, DropsTheLinkAfterTheConfiguredMissesThenReconnects) {
    int opens = 0;
    auto config = testConfig();
    config.failuresBeforeRescan = 3;
    config.listPorts = [] { return std::vector<std::string>{"COM7"}; };
    // The first device answers twice then goes quiet; the replacement is well.
    config.openPort = [&opens](const std::string&, int, int) -> std::unique_ptr<SerialLink> {
        ++opens;
        return std::make_unique<ScriptedDevice>(WireForm::BeamV0, opens == 1 ? 3 : -1);
    };
    LinkMonitor monitor(std::move(config));

    monitor.step();  // detect: consumes one of the three good pings
    ASSERT_EQ(monitor.state(), LinkState::Up);
    monitor.step();
    monitor.step();  // the device's good pings are spent
    EXPECT_EQ(monitor.state(), LinkState::Up);

    monitor.step();
    EXPECT_EQ(monitor.state(), LinkState::Degraded);
    EXPECT_EQ(monitor.consecutiveMisses(), 1);
    monitor.step();
    monitor.step();  // third miss: drop
    EXPECT_EQ(monitor.state(), LinkState::Down);
    EXPECT_EQ(monitor.stats().reconnects, 1);

    monitor.step();  // rescan finds the replacement
    EXPECT_EQ(monitor.state(), LinkState::Up);
    EXPECT_EQ(opens, 2);
}

TEST(LinkMonitor, ReportsDownWhenNoPortAnswers) {
    auto config = testConfig();
    config.listPorts = [] { return std::vector<std::string>{"COM1", "COM2"}; };
    config.openPort = [](const std::string&, int, int) -> std::unique_ptr<SerialLink> {
        return std::make_unique<DeadPort>();
    };
    std::vector<LinkState> seen;
    LinkMonitor monitor(std::move(config));
    monitor.setObserver([&seen](const LinkEvent& event) { seen.push_back(event.state); });

    monitor.step();
    EXPECT_EQ(monitor.state(), LinkState::Down);
    EXPECT_TRUE(monitor.portName().empty());
    EXPECT_EQ(monitor.link(), nullptr);
    EXPECT_FALSE(seen.empty());
}

TEST(LinkMonitor, AnUnpluggedDeviceSurfacesAsAnErrorNotASilentMiss) {
    auto* device = new ScriptedDevice(WireForm::BeamV0, -1);
    auto config = testConfig();
    config.listPorts = [] { return std::vector<std::string>{"COM7"}; };
    config.openPort = [device](const std::string&, int, int) -> std::unique_ptr<SerialLink> {
        return std::unique_ptr<SerialLink>(device);
    };
    LinkMonitor monitor(std::move(config));
    monitor.step();
    ASSERT_EQ(monitor.state(), LinkState::Up);

    std::string message;
    monitor.setObserver([&message](const LinkEvent& event) {
        if (event.state == LinkState::Degraded) message = event.message;
    });
    device->unplug();
    monitor.step();
    EXPECT_EQ(monitor.state(), LinkState::Degraded);
    // A dead port must read as a failure, not as a plain timeout.
    EXPECT_NE(message.find("General failure"), std::string::npos);
}

TEST(PingOnce, KeepsAChattyDevicesFirstLineForDiagnosis) {
    // Noise queued on write, so it survives the clearSerial drain pingOnce
    // does first -- a boot banner racing the ping is the real case.
    NoisyDevice link;
    const PingResult result = pingOnce(link, WireForm::Plain);
    EXPECT_FALSE(result.pong);
    EXPECT_EQ(result.reply, "BOOT v1.2");
}

TEST(VisibleBytes, EscapesControlCharacters) {
    EXPECT_EQ(visibleBytes(std::string("ab\r\x01")), "ab\\x0d\\x01");
}

// A device that answers a command with a burst but always reports an empty
// buffer -- the shape that made the old read loop collect nothing.
namespace {

class StreamingDevice : public SerialLink {
public:
    explicit StreamingDevice(int lines) : lines_(lines) {}

    void writeLine(const std::string& data) override {
        if (data.rfind("Correction", 0) != 0) return;
        for (int i = 0; i < lines_; ++i) inbound_.push_back("1.0,2.0");
    }
    std::string readLine() override {
        if (inbound_.empty()) return std::string();
        const std::string line = inbound_.front();
        inbound_.pop_front();
        return line;
    }
    // Lies the way a real port does in the gap before the device answers.
    std::size_t bytesAvailable() override { return 0; }
    bool isOpen() const override { return true; }

private:
    int lines_;
    std::deque<std::string> inbound_;
};

}  // namespace

TEST(SendAndCollect, ReadsTheWholeBurstWhenTheBufferLooksEmpty) {
    StreamingDevice device(2048);
    const std::vector<std::string> lines = sendAndCollect(device, "Correction");
    EXPECT_EQ(lines.size(), 2048u);
}

TEST(SendAndCollect, StopsAtTheFirstSilence) {
    StreamingDevice device(2);
    const std::vector<std::string> lines = sendAndCollect(device, "Correction");
    ASSERT_EQ(lines.size(), 2u);
    EXPECT_EQ(lines[0], "1.0,2.0");
}

TEST(SendAndCollect, StaleInputIsClearedBeforeTheCommandGoesOut) {
    FakeSerialLink link;
    link.inbound.push_back("left over");
    EXPECT_TRUE(sendAndCollect(link, "Correction").empty());
}

TEST(SendAndCollect, LeavesNothingForTheNextCommand) {
    StreamingDevice device(16);
    (void)sendAndCollect(device, "Correction");
    EXPECT_TRUE(device.readLine().empty());
}

TEST(SendAndCollect, AClosedLinkCollectsNothing) {
    FakeSerialLink link;
    link.open = false;
    EXPECT_TRUE(sendAndCollect(link, "Correction").empty());
}

// A device that says nothing for a while, then bursts -- BeamV0 waits 7 s
// before reading, so the real one behaves this way.
namespace {

class SlowStartingDevice : public SerialLink {
public:
    SlowStartingDevice(int silentReads, int lines) : silent_(silentReads), lines_(lines) {}

    void writeLine(const std::string& data) override {
        if (data.rfind("Correction", 0) == 0) armed_ = true;
    }
    std::string readLine() override {
        if (armed_ && silent_ > 0) {
            --silent_;
            return std::string();
        }
        if (armed_ && lines_ > 0) {
            --lines_;
            return "3.0,4.0";
        }
        ++afterBurst_;
        return std::string();
    }
    int readsAfterBurst() const { return afterBurst_; }
    std::size_t bytesAvailable() override { return 0; }
    bool isOpen() const override { return true; }

private:
    int silent_;
    int lines_;
    int afterBurst_ = 0;
    bool armed_ = false;
};

}  // namespace

TEST(SendAndCollect, WaitsOutTheDevicesStartupSilence) {
    SlowStartingDevice device(/*silentReads=*/6, /*lines=*/32);
    const std::vector<std::string> lines =
        sendAndCollect(device, "Correction", /*maxLines=*/100000, /*quietReads=*/10);
    EXPECT_EQ(lines.size(), 32u);
}

TEST(SendAndCollect, GivesUpOnceThePatienceIsSpent) {
    SlowStartingDevice device(/*silentReads=*/20, /*lines=*/32);
    const std::vector<std::string> lines =
        sendAndCollect(device, "Correction", /*maxLines=*/100000, /*startQuietReads=*/3);
    EXPECT_TRUE(lines.empty());
}

// The patience must not become a tail: once the burst ends, stop.
TEST(SendAndCollect, DoesNotWaitOutThePatienceAfterTheBurst) {
    SlowStartingDevice device(/*silentReads=*/2, /*lines=*/8);
    const std::vector<std::string> lines =
        sendAndCollect(device, "Correction", /*maxLines=*/100000, /*startQuietReads=*/10);
    ASSERT_EQ(lines.size(), 8u);
    EXPECT_EQ(device.readsAfterBurst(), 1);
}


// The firmware converts before echoing: percent, 10 ticks per ms, and PI/BI
// as off-times. These are the numbers it should send back.
TEST(ExpectedStimEcho, MatchesTheFirmwaresConversion) {
    SonicationTiming t;
    t.pd = 0.001;   // 1 ms   -> 10 ticks
    t.pi = 0.005;   // 5 ms   -> off 4 ms  -> 40 ticks
    t.bd = 0.100;   // 100 ms -> 1000 ticks
    t.bi = 0.500;   // 500 ms -> off 400 ms -> 4000 ticks
    t.startTime = 0.0;
    t.endTime = 30.0;  // 30 s -> 300000 ticks
    EXPECT_EQ(expectedStimEcho(t, 0.30),
              "PR:30.000 PD:10.000 PI:40.000 BD:1000.000 BI:4000.000 D:300000.000");
}

TEST(ExpectedStimEcho, DutyIsAPercentageNotAFraction) {
    SonicationTiming t;
    t.endTime = 1.0;
    EXPECT_NE(expectedStimEcho(t, 0.9).find("PR:90.000"), std::string::npos);
}

TEST(NormaliseEcho, IgnoresSpacingAndTerminators) {
    EXPECT_EQ(normaliseEcho("  PR:30.000   PD:10.000\r\n"), "PR:30.000 PD:10.000");
    EXPECT_EQ(normaliseEcho("PR:1.000  PD:2.000"), normaliseEcho(" PR:1.000 PD:2.000 "));
}

TEST(StimEchoDifferences, AgreeingFieldsProduceNothing) {
    EXPECT_TRUE(stimEchoDifferences("PR:67.000 PD:50.000", "PR:67.000 PD:50.000").empty());
}

// The device's timebase is a shade off the source we have, so D lands ~0.014%
// out on a 30 s run. That must not read as a parameter error.
TEST(StimEchoDifferences, ToleratesTheDevicesTimebaseDrift) {
    EXPECT_TRUE(stimEchoDifferences("D:300000.000", "D:299958.000").empty());
}

TEST(StimEchoDifferences, NamesAFieldThatIsGenuinelyWrong) {
    const auto diff = stimEchoDifferences("PR:67.000 BD:300.000", "PR:67.000 BD:600.000");
    ASSERT_EQ(diff.size(), 1u);
    EXPECT_NE(diff[0].find("BD"), std::string::npos);
    EXPECT_NE(diff[0].find("300"), std::string::npos);
    EXPECT_NE(diff[0].find("600"), std::string::npos);
}

TEST(ExpectedStimEcho, SubtractsInMillisecondsLikeTheDevice) {
    SonicationTiming t;
    t.pd = 0.005;  t.pi = 0.010;
    t.bd = 0.030;  t.bi = 0.700;   // 700 - 30 = 670 ms -> 6700 ticks, not 6699
    t.startTime = 0.0; t.endTime = 30.0;
    EXPECT_EQ(expectedStimEcho(t, 0.6712),
              "PR:67.000 PD:50.000 PI:50.000 BD:300.000 BI:6700.000 D:300000.000");
}
