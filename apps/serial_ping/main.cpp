// Standalone hardware check: find the STM32, then PING/PONG it until
// stopped, reconnecting on a drop.

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
#include <thread>
#include <vector>

#include "serialcom/link_monitor.hpp"
#include "serialcom/serial_port.hpp"

namespace {

using beam::serialcom::LinkMonitor;
using beam::serialcom::LinkState;
using beam::serialcom::WireForm;

std::atomic<bool> g_stop{false};

void onSignal(int) { g_stop.store(true); }

struct Options {
    LinkMonitor::Config config;
    bool listOnly = false;
    bool rawOnly = false;
    std::string command;  // --cmd NAME: send it once and time the reply
    std::string savePath;
    int startQuietReads = 10;
};

std::string nowStamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t secs = std::chrono::system_clock::to_time_t(now);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) %
                    std::chrono::seconds(1);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &secs);
#else
    localtime_r(&secs, &tm);
#endif
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%03d", tm.tm_hour, tm.tm_min, tm.tm_sec,
                  static_cast<int>(ms.count()));
    return buf;
}

void usage() {
    std::printf(
        "serial_ping -- find the Beam STM32 and PING/PONG it continuously.\n\n"
        "  --port COM7           skip auto-detect and use this port\n"
        "  --baud N              default 115200 (connectSerial.m)\n"
        "  --timeout MS          reply wait, default 700\n"
        "  --interval MS         between pings, default 1000\n"
        "  --settle MS           after opening a port, default 400 (DTR resets the MCU)\n"
        "  --failures N          consecutive misses before reconnecting, default 3\n"
        "  --form beamv0|plain   force the wire form instead of trying both\n"
        "  --list                list available ports and exit\n"
        "  --raw                 listen, then send each form and hex-dump the\n"
        "                        bytes that come back; needs --port\n"
        "  --cmd NAME            send one command, time it, report the reply\n"
        "  --patience N          empty reads to tolerate before the first line\n"
        "  --save FILE           write the reply lines to FILE\n");
}

bool parse(int argc, char** argv, Options* options) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") return false;
        if (arg == "--list") {
            options->listOnly = true;
            continue;
        }
        if (arg == "--raw") {
            options->rawOnly = true;
            continue;
        }
        if (i + 1 >= argc) {
            std::printf("missing value for %s\n", arg.c_str());
            return false;
        }
        const std::string value = argv[++i];
        LinkMonitor::Config& config = options->config;
        if (arg == "--port") {
            config.forcedPort = value;
        } else if (arg == "--baud") {
            config.baudRate = std::atoi(value.c_str());
        } else if (arg == "--timeout") {
            config.replyTimeoutMs = std::atoi(value.c_str());
        } else if (arg == "--interval") {
            config.pingIntervalMs = std::atoi(value.c_str());
        } else if (arg == "--settle") {
            config.settleMs = std::atoi(value.c_str());
        } else if (arg == "--failures") {
            config.failuresBeforeRescan = std::atoi(value.c_str());
        } else if (arg == "--cmd") {
            options->command = value;
        } else if (arg == "--save") {
            options->savePath = value;
        } else if (arg == "--patience") {
            options->startQuietReads = std::atoi(value.c_str());
        } else if (arg == "--form") {
            config.autoDetectForm = false;
            if (value == "beamv0") {
                config.form = WireForm::BeamV0;
            } else if (value == "plain") {
                config.form = WireForm::Plain;
            } else {
                std::printf("unknown --form %s\n", value.c_str());
                return false;
            }
        } else {
            std::printf("unknown option %s\n", arg.c_str());
            return false;
        }
    }
    return true;
}

// readLine cannot tell silence from a reply with no LF; this can.
int runRaw(const Options& options) {
    const std::string port =
        options.config.forcedPort.empty() ? std::string() : options.config.forcedPort;
    if (port.empty()) {
        std::printf("--raw needs --port COM4\n");
        return 2;
    }
    const auto dump = [](const std::string& bytes) {
        if (bytes.empty()) {
            std::printf("    (nothing)\n");
            return;
        }
        std::printf("    %zu byte(s)\n    hex:", bytes.size());
        for (const char c : bytes) std::printf(" %02x", static_cast<unsigned char>(c));
        std::printf("\n    txt: ");
        for (const char c : bytes)
            std::printf("%c", (c >= 0x20 && c < 0x7f) ? c : '.');
        std::printf("\n");
    };

    try {
        std::printf("opening %s at %d baud\n", port.c_str(), options.config.baudRate);
        beam::serialcom::SerialPort link(port, options.config.baudRate,
                                         options.config.replyTimeoutMs);
        std::printf("listening %d ms for anything unprompted (boot banner?)\n",
                    options.config.settleMs);
        dump(link.readRaw(options.config.settleMs));

        for (const WireForm form : {WireForm::BeamV0, WireForm::Plain}) {
            const char* payload = beam::serialcom::formPayload(form);
            std::printf("sending %s -- bytes: ", beam::serialcom::formName(form));
            for (const char* p = payload; *p != 0; ++p)
                std::printf("%02x ", static_cast<unsigned char>(*p));
            std::printf("0a\n");
            link.writeLine(payload);
            dump(link.readRaw(options.config.replyTimeoutMs));
        }
    } catch (const std::exception& e) {
        std::printf("failed: %s\n", e.what());
        return 1;
    }
    return 0;
}

// One command, timed, so the cost of a correction is a measurement and not
// an estimate.
int runCommandOnce(const Options& options) {
    std::vector<std::string> ports;
    if (!options.config.forcedPort.empty()) {
        ports.push_back(options.config.forcedPort);
    } else {
        ports = beam::serialcom::listAvailableComPorts();
    }
    for (const std::string& name : ports) {
        try {
            beam::serialcom::SerialPort link(name, options.config.baudRate,
                                             options.config.replyTimeoutMs);
            // A device that will not answer PING will not answer anything.
            if (!beam::serialcom::pingOnce(link, WireForm::BeamV0).pong &&
                !beam::serialcom::pingOnce(link, WireForm::Plain).pong) {
                continue;
            }
            std::printf("%s: sending %s\n", name.c_str(), options.command.c_str());
            const auto started = std::chrono::steady_clock::now();
            const std::vector<std::string> reply = beam::serialcom::sendAndCollect(
                link, options.command, 1000000, options.startQuietReads);
            const double ms = std::chrono::duration<double, std::milli>(
                                  std::chrono::steady_clock::now() - started)
                                  .count();

            std::size_t bytes = 0;
            std::size_t values = 0;
            for (const std::string& line : reply) {
                bytes += line.size();
                values += 1 + std::count(line.begin(), line.end(), ',');
            }
            std::printf("  %zu lines, %zu values, %zu bytes in %.0f ms\n", reply.size(), values,
                        bytes, ms);
            if (!options.savePath.empty()) {
                std::FILE* out = std::fopen(options.savePath.c_str(), "w");
                if (out != nullptr) {
                    for (const std::string& line : reply) std::fprintf(out, "%s\n", line.c_str());
                    std::fclose(out);
                    std::printf("  saved %zu lines to %s\n", reply.size(), options.savePath.c_str());
                }
            }
            if (!reply.empty()) {
                std::printf("  first: %s\n", reply.front().c_str());
                std::printf("  last:  %s\n", reply.back().c_str());
            }
            // The link has to survive the command, which is the whole point.
            const beam::serialcom::PingResult after =
                beam::serialcom::pingOnce(link, WireForm::BeamV0);
            std::printf("  ping after: %s\n", after.pong ? "PONG" : "no reply");
            return reply.empty() ? 1 : 0;
        } catch (const std::exception& e) {
            std::printf("%s: %s\n", name.c_str(), e.what());
        }
    }
    std::printf("no device answered\n");
    return 1;
}

}  // namespace

int main(int argc, char** argv) {
    Options options;
    if (!parse(argc, argv, &options)) {
        usage();
        return 2;
    }
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);
    std::setvbuf(stdout, nullptr, _IONBF, 0);  // _IOLBF with size 0 aborts on MSVC

    if (options.listOnly) {
        const std::vector<std::string> ports = beam::serialcom::listAvailableComPorts();
        std::printf("%zu available port(s)\n", ports.size());
        for (const std::string& name : ports) std::printf("  %s\n", name.c_str());
        return ports.empty() ? 1 : 0;
    }

    if (options.rawOnly) return runRaw(options);
    if (!options.command.empty()) return runCommandOnce(options);

    std::printf("serial_ping -- Ctrl+C to stop\n");

    LinkMonitor monitor(options.config);
    monitor.setObserver([](const beam::serialcom::LinkEvent& event) {
        std::printf("[%s] %-5s %s\n", nowStamp().c_str(),
                    beam::serialcom::describe(event.state), event.message.c_str());
    });

    while (!g_stop.load()) {
        monitor.step();
        const int sliceMs = 25;
        for (int waited = 0; waited < monitor.sleepHintMs() && !g_stop.load(); waited += sliceMs)
            std::this_thread::sleep_for(std::chrono::milliseconds(sliceMs));
    }

    const LinkMonitor::Stats stats = monitor.stats();
    std::printf("\nstopped. sent %lld, pong %lld, missed %lld, reconnects %lld\n", stats.sent,
                stats.pongs, stats.misses, stats.reconnects);
    return stats.pongs > 0 ? 0 : 1;
}
