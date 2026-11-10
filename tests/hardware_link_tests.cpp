// Tests that talk to the real board over its COM port.
//
// SAFETY. These send one command only: PING. The firmware also understands
// SCAN, which drives the transducers -- it is deliberately absent here and
// must stay absent. A test suite is not a place to energise an array.
//
// Every test skips when no board is attached, so the suite still passes on a
// machine without hardware. A port held by another program (the app, a
// terminal) also skips rather than fails: that is the environment being busy,
// not the code being wrong.
//
// Run just these:
//   unit_tests.exe --gtest_filter='HardwareLink.*'

#include <algorithm>
#include <chrono>
#include <thread>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "serialcom/serial_port.hpp"

namespace {

// The ST-Link debugger on a NUCLEO board.
constexpr const char* kStVendorId = "0483";

struct FoundBoard {
    std::string port;
    beam::serialcom::ComPortIdentity identity;
    bool found = false;
};

FoundBoard findBoard() {
    FoundBoard result;
    for (const std::string& port : beam::serialcom::listAllComPorts()) {
        const beam::serialcom::ComPortIdentity identity =
            beam::serialcom::identifyComPort(port);
        const bool isBoard = identity.vendorId == kStVendorId ||
                             identity.board.find("NUCLEO") != std::string::npos;
        if (!isBoard) continue;
        result.port = port;
        result.identity = identity;
        result.found = true;
        return result;
    }
    return result;
}

// Opens the board, or skips the calling test with the reason. Returns nullptr
// when it skipped, so callers return immediately.
std::unique_ptr<beam::serialcom::SerialPort> openBoardOrSkip(std::string* portOut = nullptr) {
    const FoundBoard board = findBoard();
    if (!board.found) return nullptr;
    if (portOut != nullptr) *portOut = board.port;
    try {
        return std::make_unique<beam::serialcom::SerialPort>(board.port);
    } catch (const beam::serialcom::SerialOpenError&) {
        return nullptr;  // held by another program
    }
}

#define SKIP_WITHOUT_BOARD()                                                          \
    do {                                                                              \
        if (!findBoard().found) GTEST_SKIP() << "no NUCLEO board on any COM port";    \
    } while (false)

}  // namespace

TEST(HardwareLink, TheBoardIsEnumeratedWithItsIdentity) {
    SKIP_WITHOUT_BOARD();
    const FoundBoard board = findBoard();
    EXPECT_FALSE(board.port.empty());
    // The USB identity is what the treatment log records as the device serial,
    // so an empty one would file a sonication against no hardware.
    EXPECT_FALSE(board.identity.serial.empty()) << "port " << board.port;
    EXPECT_EQ(board.identity.vendorId, kStVendorId);
}

TEST(HardwareLink, AnEnumeratedBoardPortCanBeOpened) {
    SKIP_WITHOUT_BOARD();
    std::string port;
    const auto link = openBoardOrSkip(&port);
    if (link == nullptr) GTEST_SKIP() << "board port is held by another program";
    EXPECT_TRUE(link->isOpen());
}

TEST(HardwareLink, TheBoardAnswersPingWithPong) {
    SKIP_WITHOUT_BOARD();
    const auto link = openBoardOrSkip();
    if (link == nullptr) GTEST_SKIP() << "board port is held by another program";
    EXPECT_TRUE(beam::serialcom::isSerialHealthy(*link));
}

// The link monitor polls continuously, so one good reply proves little.
TEST(HardwareLink, PingIsAnsweredRepeatedly) {
    SKIP_WITHOUT_BOARD();
    const auto link = openBoardOrSkip();
    if (link == nullptr) GTEST_SKIP() << "board port is held by another program";
    int answered = 0;
    for (int attempt = 0; attempt < 5; ++attempt)
        if (beam::serialcom::isSerialHealthy(*link)) ++answered;
    EXPECT_EQ(answered, 5);
}

// A quiet link must time out and return, not block. A hang here would freeze
// the GUI thread the monitor runs on.
TEST(HardwareLink, ReadingAQuietLinkTimesOutRatherThanBlocking) {
    SKIP_WITHOUT_BOARD();
    const auto link = openBoardOrSkip();
    if (link == nullptr) GTEST_SKIP() << "board port is held by another program";
    beam::serialcom::clearSerial(*link);
    const auto started = std::chrono::steady_clock::now();
    const std::string quiet = link->readLine();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - started)
                             .count();
    EXPECT_TRUE(quiet.empty()) << "unsolicited line: " << quiet;
    // The port is constructed with a 1000 ms read timeout.
    EXPECT_LT(elapsed, 3000) << "readLine blocked for " << elapsed << " ms";
}

// Draining must leave the link usable; the app clears before every exchange.
TEST(HardwareLink, ClearingLeavesTheLinkUsable) {
    SKIP_WITHOUT_BOARD();
    const auto link = openBoardOrSkip();
    if (link == nullptr) GTEST_SKIP() << "board port is held by another program";
    beam::serialcom::clearSerial(*link);
    EXPECT_TRUE(beam::serialcom::isSerialHealthy(*link));
}

// Two openers of one port is the app plus a stray terminal. The second must
// fail loudly rather than return a link that silently reads nothing.
TEST(HardwareLink, TheOpenPortIsExclusive) {
    SKIP_WITHOUT_BOARD();
    std::string port;
    const auto held = openBoardOrSkip(&port);
    if (held == nullptr) GTEST_SKIP() << "board port is held by another program";
    EXPECT_THROW(beam::serialcom::SerialPort second(port), beam::serialcom::SerialOpenError);
    EXPECT_TRUE(held->isOpen()) << "the failed open disturbed the held link";
}

// Closing and reopening is what reconnection does after a cable pull.
TEST(HardwareLink, TheBoardCanBeReopenedAfterClosing) {
    SKIP_WITHOUT_BOARD();
    std::string port;
    {
        const auto first = openBoardOrSkip(&port);
        if (first == nullptr) GTEST_SKIP() << "board port is held by another program";
        EXPECT_TRUE(beam::serialcom::isSerialHealthy(*first));
    }  // closed here
    beam::serialcom::SerialPort again(port);
    EXPECT_TRUE(again.isOpen());
    EXPECT_TRUE(beam::serialcom::isSerialHealthy(again));
}

// listAvailableComPorts promises only ports that can be opened now.
TEST(HardwareLink, AvailablePortsAreASubsetOfAllPorts) {
    SKIP_WITHOUT_BOARD();
    const std::vector<std::string> all = beam::serialcom::listAllComPorts();
    for (const std::string& port : beam::serialcom::listAvailableComPorts())
        EXPECT_NE(std::find(all.begin(), all.end(), port), all.end())
            << port << " is openable but absent from listAllComPorts()";
}

// readRaw is how the app tells silence from a reply missing its terminator,
// so both of its outcomes matter.
TEST(HardwareLink, ReadRawReturnsNothingOnASilentLink) {
    SKIP_WITHOUT_BOARD();
    const auto link = openBoardOrSkip();
    if (link == nullptr) GTEST_SKIP() << "board port is held by another program";
    beam::serialcom::clearSerial(*link);
    EXPECT_TRUE(link->readRaw(200).empty());
}

TEST(HardwareLink, ReadRawCollectsTheReplyToPing) {
    SKIP_WITHOUT_BOARD();
    const auto link = openBoardOrSkip();
    if (link == nullptr) GTEST_SKIP() << "board port is held by another program";
    beam::serialcom::clearSerial(*link);
    beam::serialcom::sendSerialCommand(*link, "PING");
    const std::string raw = link->readRaw(500);
    EXPECT_NE(raw.find("PONG"), std::string::npos) << "raw reply was: " << raw;
}

// bytesAvailable drives the drain loop; it has to rise on a reply and reach
// zero once drained, or clearSerial would spin or stop early.
TEST(HardwareLink, BytesAvailableRisesOnAReplyAndClearsOnDrain) {
    SKIP_WITHOUT_BOARD();
    const auto link = openBoardOrSkip();
    if (link == nullptr) GTEST_SKIP() << "board port is held by another program";
    beam::serialcom::clearSerial(*link);
    beam::serialcom::sendSerialCommand(*link, "PING");
    std::size_t peak = 0;
    for (int wait = 0; wait < 20 && peak == 0; ++wait) {
        peak = link->bytesAvailable();
        if (peak == 0) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    EXPECT_GT(peak, 0u);
    beam::serialcom::clearSerial(*link);
    // clearSerial deliberately leaves at most one byte behind.
    EXPECT_LE(link->bytesAvailable(), 1u);
}

// sendAndCollect is the path the app uses for multi-line replies.
TEST(HardwareLink, SendAndCollectGetsThePingReply) {
    SKIP_WITHOUT_BOARD();
    const auto link = openBoardOrSkip();
    if (link == nullptr) GTEST_SKIP() << "board port is held by another program";
    const std::vector<std::string> lines = beam::serialcom::sendAndCollect(*link, "PING");
    ASSERT_FALSE(lines.empty());
    EXPECT_NE(lines.front().find("PONG"), std::string::npos) << "first line: " << lines.front();
}
