#include "serialcom/serial_port.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <stdexcept>
#include <thread>

namespace beam::serialcom {

// ---------------------------------------------------------------------------
// Platform-independent command flow
// ---------------------------------------------------------------------------

namespace {

bool startsWithPong(const std::string& s) {
    if (s.size() < 4) {
        return false;
    }
    static const char kPong[4] = {'P', 'O', 'N', 'G'};
    for (int i = 0; i < 4; ++i) {
        if (std::toupper(static_cast<unsigned char>(s[static_cast<std::size_t>(i)])) != kPong[i]) {
            return false;
        }
    }
    return true;
}

}  // namespace

void clearSerial(SerialLink& link) {
    // The `> 1` (not `> 0`) is deliberate. The empty-read guard stops a
    // partial line with no terminator spinning here forever.
    while (link.bytesAvailable() > 1) {
        if (link.readLine().empty()) {
            break;
        }
    }
}

bool sendSerialCommand(SerialLink& link, const std::string& cmd) {
    clearSerial(link);
    if (!link.isOpen()) {
        return false;
    }
    // The literal backslash and n are protocol, not a typo; writeLine adds
    // the real LF after them. See the header.
    link.writeLine(cmd + "\\n");
    return true;
}

bool isSerialHealthy(SerialLink& link) {
    if (!link.isOpen()) {
        return false;
    }
    try {
        sendSerialCommand(link, "PING");
        return startsWithPong(link.readLine());
    } catch (...) {
        return false;
    }
}

}  // namespace beam::serialcom

// ---------------------------------------------------------------------------
// OS-backed SerialPort + listAvailableComPorts
// ---------------------------------------------------------------------------

#if defined(_WIN32)

#include <windows.h>

#include <cfgmgr32.h>
#include <devguid.h>
#include <setupapi.h>

namespace beam::serialcom {

namespace {

// The bare "cannot open" tells an operator nothing. 2 = no such port,
// 5 = another program holds it; both are everyday bring-up answers.
std::string describeLastError(DWORD code) {
    char* text = nullptr;
    const DWORD n = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, 0, reinterpret_cast<char*>(&text), 0, nullptr);
    std::string message = "error " + std::to_string(code);
    if (n > 0 && text != nullptr) {
        std::string detail(text, n);
        while (!detail.empty() && (detail.back() == '\r' || detail.back() == '\n' ||
                                   detail.back() == ' ' || detail.back() == '.'))
            detail.pop_back();
        if (!detail.empty()) message += ": " + detail;
    }
    if (text != nullptr) LocalFree(text);
    return message;
}

}  // namespace

struct SerialPort::Impl {
    HANDLE handle = INVALID_HANDLE_VALUE;
    int timeoutMs = 1000;
};

SerialPort::SerialPort(const std::string& portName, int baudRate, int readTimeoutMs)
    : impl_(std::make_unique<Impl>()) {
    impl_->timeoutMs = readTimeoutMs;

    // The "\\.\COMn" form is required for COM10 and above and works for all.
    std::string path = portName;
    if (path.rfind("\\\\.\\", 0) != 0) {
        path = "\\\\.\\" + path;
    }

    impl_->handle = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0,
                                nullptr);
    if (impl_->handle == INVALID_HANDLE_VALUE) {
        const DWORD code = GetLastError();
        const OpenFailure reason = code == ERROR_ACCESS_DENIED    ? OpenFailure::InUse
                                   : code == ERROR_FILE_NOT_FOUND ? OpenFailure::NotFound
                                                                  : OpenFailure::Other;
        throw SerialOpenError("cannot open " + portName + ": " + describeLastError(code),
                              reason);
    }

    DCB dcb{};
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(impl_->handle, &dcb)) {
        CloseHandle(impl_->handle);
        impl_->handle = INVALID_HANDLE_VALUE;
        throw std::runtime_error("SerialPort: GetCommState failed for " + portName);
    }
    dcb.BaudRate = static_cast<DWORD>(baudRate);
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary = TRUE;
    dcb.fParity = FALSE;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fDtrControl = DTR_CONTROL_ENABLE;
    dcb.fRtsControl = RTS_CONTROL_ENABLE;
    dcb.fInX = FALSE;
    dcb.fOutX = FALSE;
    if (!SetCommState(impl_->handle, &dcb)) {
        CloseHandle(impl_->handle);
        impl_->handle = INVALID_HANDLE_VALUE;
        throw std::runtime_error("SerialPort: SetCommState failed for " + portName);
    }

    // Non-blocking reads: ReadFile returns immediately with whatever is
    // buffered; readLine() does its own timed polling loop.
    COMMTIMEOUTS timeouts{};
    timeouts.ReadIntervalTimeout = MAXDWORD;
    timeouts.ReadTotalTimeoutConstant = 0;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    timeouts.WriteTotalTimeoutConstant = 1000;
    timeouts.WriteTotalTimeoutMultiplier = 0;
    SetCommTimeouts(impl_->handle, &timeouts);

    PurgeComm(impl_->handle, PURGE_RXCLEAR | PURGE_TXCLEAR);
}

SerialPort::~SerialPort() {
    if (impl_->handle != INVALID_HANDLE_VALUE) {
        CloseHandle(impl_->handle);
    }
}

void SerialPort::writeLine(const std::string& data) {
    std::string buf = data;
    buf.push_back('\n');
    const char* p = buf.data();
    DWORD remaining = static_cast<DWORD>(buf.size());
    while (remaining > 0) {
        DWORD written = 0;
        if (!WriteFile(impl_->handle, p, remaining, &written, nullptr)) {
            throw std::runtime_error("SerialPort: write error");
        }
        p += written;
        remaining -= written;
    }
}

std::string SerialPort::readLine() {
    std::string line;
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(impl_->timeoutMs);
    for (;;) {
        char c = 0;
        DWORD got = 0;
        if (!ReadFile(impl_->handle, &c, 1, &got, nullptr)) {
            throw std::runtime_error("SerialPort: read error");
        }
        if (got == 1) {
            if (c == '\n') {
                break;
            }
            line.push_back(c);
        } else {
            if (std::chrono::steady_clock::now() >= deadline) {
                return std::string();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
    return line;
}

std::string SerialPort::readRaw(int windowMs) {
    std::string out;
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(windowMs);
    while (std::chrono::steady_clock::now() < deadline) {
        char buf[256];
        DWORD got = 0;
        if (!ReadFile(impl_->handle, buf, sizeof(buf), &got, nullptr)) break;
        if (got > 0) {
            out.append(buf, got);
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
    return out;
}

std::size_t SerialPort::bytesAvailable() {
    if (impl_->handle == INVALID_HANDLE_VALUE) {
        return 0;
    }
    COMSTAT status{};
    DWORD errors = 0;
    if (!ClearCommError(impl_->handle, &errors, &status)) {
        return 0;
    }
    return status.cbInQue;
}

bool SerialPort::isOpen() const { return impl_->handle != INVALID_HANDLE_VALUE; }

namespace {

// Shared by the two listings: the registry names every port that exists;
// `requireOpenable` is what makes it the "available" subset.
std::vector<std::string> enumerateComPorts(bool requireOpenable) {
    std::vector<std::string> ports;
    HKEY key = nullptr;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "HARDWARE\\DEVICEMAP\\SERIALCOMM", 0, KEY_READ, &key) !=
        ERROR_SUCCESS) {
        return ports;
    }
    for (DWORD i = 0;; ++i) {
        char name[256];
        char data[256];
        DWORD nameLen = sizeof(name);
        DWORD dataLen = sizeof(data);
        DWORD type = 0;
        const LONG r = RegEnumValueA(key, i, name, &nameLen, nullptr, &type,
                                     reinterpret_cast<BYTE*>(data), &dataLen);
        if (r == ERROR_NO_MORE_ITEMS) {
            break;
        }
        if (r != ERROR_SUCCESS || type != REG_SZ) {
            continue;
        }
        const std::string com(data);
        if (requireOpenable) {
            const std::string path = "\\\\.\\" + com;
            HANDLE h = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                   OPEN_EXISTING, 0, nullptr);
            if (h == INVALID_HANDLE_VALUE) {
                continue;
            }
            CloseHandle(h);
        }
        ports.push_back(com);
    }
    RegCloseKey(key);
    std::sort(ports.begin(), ports.end());
    return ports;
}

}  // namespace

// The serial number lives on the parent composite device, not on the
// interface that owns the COM name.
ComPortIdentity identifyComPort(const std::string& portName) {
    ComPortIdentity identity;
    const HDEVINFO set = SetupDiGetClassDevsA(&GUID_DEVCLASS_PORTS, nullptr, nullptr, DIGCF_PRESENT);
    if (set == INVALID_HANDLE_VALUE) {
        return identity;
    }

    SP_DEVINFO_DATA info{};
    info.cbSize = sizeof(info);
    for (DWORD i = 0; SetupDiEnumDeviceInfo(set, i, &info); ++i) {
        const HKEY key = SetupDiOpenDevRegKey(set, &info, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
        if (key == INVALID_HANDLE_VALUE) {
            continue;
        }
        char name[64];
        DWORD size = sizeof(name);
        DWORD type = 0;
        const LONG r = RegQueryValueExA(key, "PortName", nullptr, &type,
                                        reinterpret_cast<BYTE*>(name), &size);
        RegCloseKey(key);
        if (r != ERROR_SUCCESS || type != REG_SZ || portName != name) {
            continue;
        }

        DEVINST parent = 0;
        if (CM_Get_Parent(&parent, info.DevInst, 0) != CR_SUCCESS) {
            break;
        }
        char id[MAX_DEVICE_ID_LEN];
        if (CM_Get_Device_IDA(parent, id, sizeof(id), 0) != CR_SUCCESS) {
            break;
        }
        // USB\VID_0483&PID_374B\066FFF343433464757204334
        const std::string text(id);
        const std::size_t vid = text.find("VID_");
        const std::size_t pid = text.find("PID_");
        if (vid != std::string::npos) identity.vendorId = text.substr(vid + 4, 4);
        if (pid != std::string::npos) identity.productId = text.substr(pid + 4, 4);
        const std::size_t last = text.rfind('\\');
        if (last != std::string::npos && last + 1 < text.size())
            identity.serial = text.substr(last + 1);
        break;
    }

    SetupDiDestroyDeviceInfoList(set);
    return identity;
}

std::vector<std::string> listAvailableComPorts() { return enumerateComPorts(true); }

std::vector<std::string> listAllComPorts() { return enumerateComPorts(false); }

}  // namespace beam::serialcom

#elif defined(__unix__) || defined(__APPLE__)

#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

namespace beam::serialcom {

namespace {

speed_t toSpeed(int baud) {
    switch (baud) {
        case 9600: return B9600;
        case 19200: return B19200;
        case 38400: return B38400;
        case 57600: return B57600;
        case 115200: return B115200;
        case 230400: return B230400;
        default: return B0;
    }
}

}  // namespace

struct SerialPort::Impl {
    int fd = -1;
    int timeoutMs = 1000;
};

SerialPort::SerialPort(const std::string& portName, int baudRate, int readTimeoutMs)
    : impl_(std::make_unique<Impl>()) {
    impl_->timeoutMs = readTimeoutMs;

    impl_->fd = ::open(portName.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (impl_->fd < 0) {
        const int code = errno;
        const OpenFailure reason = (code == EACCES || code == EBUSY) ? OpenFailure::InUse
                                   : code == ENOENT                 ? OpenFailure::NotFound
                                                                    : OpenFailure::Other;
        throw SerialOpenError("cannot open " + portName + ": " + std::strerror(code), reason);
    }

    const speed_t sp = toSpeed(baudRate);
    if (sp == B0) {
        ::close(impl_->fd);
        impl_->fd = -1;
        throw std::runtime_error("SerialPort: unsupported baud rate " + std::to_string(baudRate));
    }

    termios tio{};
    if (tcgetattr(impl_->fd, &tio) != 0) {
        ::close(impl_->fd);
        impl_->fd = -1;
        throw std::runtime_error("SerialPort: tcgetattr failed");
    }
    cfmakeraw(&tio);
    cfsetispeed(&tio, sp);
    cfsetospeed(&tio, sp);
    tio.c_cflag |= (CLOCAL | CREAD);
    tio.c_cflag &= ~static_cast<tcflag_t>(CRTSCTS);
    tio.c_cflag &= ~static_cast<tcflag_t>(PARENB | CSTOPB | CSIZE);
    tio.c_cflag |= CS8;
    tio.c_cc[VMIN] = 0;
    tio.c_cc[VTIME] = 0;
    if (tcsetattr(impl_->fd, TCSANOW, &tio) != 0) {
        ::close(impl_->fd);
        impl_->fd = -1;
        throw std::runtime_error("SerialPort: tcsetattr failed");
    }
    tcflush(impl_->fd, TCIOFLUSH);
}

SerialPort::~SerialPort() {
    if (impl_->fd >= 0) {
        ::close(impl_->fd);
    }
}

void SerialPort::writeLine(const std::string& data) {
    std::string buf = data;
    buf.push_back('\n');
    std::size_t off = 0;
    while (off < buf.size()) {
        const ssize_t n = ::write(impl_->fd, buf.data() + off, buf.size() - off);
        if (n < 0) {
            if (errno == EAGAIN || errno == EINTR) {
                continue;
            }
            throw std::runtime_error("SerialPort: write error");
        }
        off += static_cast<std::size_t>(n);
    }
}

std::string SerialPort::readLine() {
    std::string line;
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(impl_->timeoutMs);
    for (;;) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            return std::string();
        }
        const auto remainMs =
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
        fd_set rd;
        FD_ZERO(&rd);
        FD_SET(impl_->fd, &rd);
        timeval tv;
        tv.tv_sec = static_cast<long>(remainMs / 1000);
        tv.tv_usec = static_cast<long>((remainMs % 1000) * 1000);
        const int s = ::select(impl_->fd + 1, &rd, nullptr, nullptr, &tv);
        if (s < 0) {
            if (errno == EINTR) {
                continue;
            }
            throw std::runtime_error("SerialPort: select error");
        }
        if (s == 0) {
            return std::string();
        }
        char c = 0;
        const ssize_t n = ::read(impl_->fd, &c, 1);
        if (n < 0) {
            if (errno == EAGAIN || errno == EINTR) {
                continue;
            }
            throw std::runtime_error("SerialPort: read error");
        }
        if (n == 0) {
            continue;
        }
        if (c == '\n') {
            break;
        }
        line.push_back(c);
    }
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
    return line;
}

std::string SerialPort::readRaw(int windowMs) {
    std::string out;
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(windowMs);
    while (std::chrono::steady_clock::now() < deadline) {
        char buf[256];
        const ssize_t n = ::read(impl_->fd, buf, sizeof(buf));
        if (n > 0) {
            out.append(buf, static_cast<std::size_t>(n));
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
    return out;
}

std::size_t SerialPort::bytesAvailable() {
    if (impl_->fd < 0) {
        return 0;
    }
    int n = 0;
    if (::ioctl(impl_->fd, FIONREAD, &n) < 0 || n < 0) {
        return 0;
    }
    return static_cast<std::size_t>(n);
}

bool SerialPort::isOpen() const { return impl_->fd >= 0; }

namespace {

// `requireOpenable` is what separates the "available" subset from every
// port the system has.
std::vector<std::string> enumerateComPorts(bool requireOpenable) {
    std::vector<std::string> ports;
    DIR* dir = ::opendir("/dev");
    if (dir == nullptr) {
        return ports;
    }
    for (dirent* e = ::readdir(dir); e != nullptr; e = ::readdir(dir)) {
        const std::string n = e->d_name;
        const bool match = n.rfind("ttyUSB", 0) == 0 || n.rfind("ttyACM", 0) == 0 ||
                           n.rfind("ttyS", 0) == 0 || n.rfind("cu.", 0) == 0 || n.rfind("tty.", 0) == 0;
        if (!match) {
            continue;
        }
        const std::string path = "/dev/" + n;
        if (requireOpenable) {
            const int fd = ::open(path.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
            if (fd < 0) {
                continue;
            }
            ::close(fd);
        }
        ports.push_back(path);
    }
    ::closedir(dir);
    std::sort(ports.begin(), ports.end());
    return ports;
}

}  // namespace

std::vector<std::string> listAvailableComPorts() { return enumerateComPorts(true); }

std::vector<std::string> listAllComPorts() { return enumerateComPorts(false); }

}  // namespace beam::serialcom

#else  // unknown platform

namespace beam::serialcom {

struct SerialPort::Impl {};

SerialPort::SerialPort(const std::string& portName, int, int) : impl_(nullptr) {
    throw std::runtime_error("SerialPort: no serial backend for this platform (" + portName + ")");
}
SerialPort::~SerialPort() = default;
void SerialPort::writeLine(const std::string&) { throw std::runtime_error("SerialPort: unsupported"); }
std::string SerialPort::readLine() { throw std::runtime_error("SerialPort: unsupported"); }
std::string SerialPort::readRaw(int) { throw std::runtime_error("SerialPort: unsupported"); }
std::size_t SerialPort::bytesAvailable() { return 0; }
bool SerialPort::isOpen() const { return false; }

std::vector<std::string> listAvailableComPorts() { return {}; }

std::vector<std::string> listAllComPorts() { return {}; }

}  // namespace beam::serialcom

#endif
