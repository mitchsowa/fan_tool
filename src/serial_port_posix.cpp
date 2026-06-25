// serial_port_posix.cpp - POSIX (termios) implementation of SerialPort.
// Compiled on non-Windows platforms.
#ifndef _WIN32

#include "serial_port.h"

#include <dirent.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/stat.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fstream>

namespace fan {

namespace {
// Map a baud rate to the matching termios speed_t constant. Returns false if
// the requested rate is not a standard POSIX speed.
bool baud_to_speed(unsigned baud, speed_t& out) {
    switch (baud) {
        case 1200:   out = B1200;   return true;
        case 2400:   out = B2400;   return true;
        case 4800:   out = B4800;   return true;
        case 9600:   out = B9600;   return true;
        case 19200:  out = B19200;  return true;
        case 38400:  out = B38400;  return true;
        case 57600:  out = B57600;  return true;
        case 115200: out = B115200; return true;
        case 230400: out = B230400; return true;
        default:     return false;
    }
}

// Read the first line of a small sysfs/text file, trimmed. Returns "" if the
// file is absent or unreadable.
std::string read_first_line(const std::string& path) {
    std::ifstream f(path);
    std::string line;
    if (!std::getline(f, line)) return "";
    size_t e = line.find_last_not_of(" \t\r\n");
    return (e == std::string::npos) ? "" : line.substr(0, e + 1);
}

// Basename of the target of a symlink (e.g. the driver name behind
// /sys/class/tty/ttyUSB0/device/driver -> ".../ftdi_sio").
std::string symlink_basename(const std::string& path) {
    char buf[1024];
    ssize_t n = ::readlink(path.c_str(), buf, sizeof(buf) - 1);
    if (n <= 0) return "";
    buf[n] = '\0';
    std::string target(buf);
    size_t slash = target.find_last_of('/');
    return (slash == std::string::npos) ? target : target.substr(slash + 1);
}

// Build a human-readable description for a tty by walking sysfs: prefer the USB
// manufacturer/product strings, otherwise fall back to the kernel driver name.
std::string describe_tty(const std::string& name) {
    const std::string base = "/sys/class/tty/" + name + "/device";

    // USB serial adapters expose product/manufacturer a couple of levels up
    // (tty -> usb-interface -> usb-device). Probe both ".." and "../..".
    for (const std::string& up : {std::string("/.."), std::string("/../..")}) {
        std::string product = read_first_line(base + up + "/product");
        if (!product.empty()) {
            std::string mfr = read_first_line(base + up + "/manufacturer");
            return mfr.empty() ? product : mfr + " " + product;
        }
    }

    std::string driver = symlink_basename(base + "/driver");
    // Newer kernels expose a generic "serial-base" wrapper driver literally
    // named "port" for built-in UARTs - that conveys nothing, so drop it.
    if (driver == "port") return "";
    return driver;  // "" if even the driver link is missing
}
}  // namespace

SerialPort::~SerialPort() { close(); }

SerialPort::SerialPort(SerialPort&& other) noexcept
    : fd_(other.fd_), device_(std::move(other.device_)) {
    other.fd_ = -1;
}

SerialPort& SerialPort::operator=(SerialPort&& other) noexcept {
    if (this != &other) {
        close();
        fd_ = other.fd_;
        device_ = std::move(other.device_);
        other.fd_ = -1;
    }
    return *this;
}

bool SerialPort::is_open() const { return fd_ >= 0; }

void SerialPort::open(const std::string& device, const SerialConfig& config) {
    close();

    int fd = ::open(device.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        throw SerialError("cannot open " + device + ": " + std::strerror(errno));
    }

    // Switch back to blocking semantics; we manage timeouts with select().
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);

    termios tty{};
    if (tcgetattr(fd, &tty) != 0) {
        std::string err = std::strerror(errno);
        ::close(fd);
        throw SerialError("tcgetattr failed on " + device + ": " + err);
    }

    speed_t speed;
    if (!baud_to_speed(config.baud, speed)) {
        ::close(fd);
        throw SerialError("unsupported baud rate: " + std::to_string(config.baud));
    }
    cfsetispeed(&tty, speed);
    cfsetospeed(&tty, speed);

    // Raw mode (8N1 by default), no flow control, no signal/echo processing.
    cfmakeraw(&tty);

    tty.c_cflag |= (CLOCAL | CREAD);
    tty.c_cflag &= ~CSIZE;
    switch (config.data_bits) {
        case 7: tty.c_cflag |= CS7; break;
        case 8: tty.c_cflag |= CS8; break;
        default:
            ::close(fd);
            throw SerialError("unsupported data bits: " +
                              std::to_string(config.data_bits));
    }

    if (config.stop_bits == 2) {
        tty.c_cflag |= CSTOPB;
    } else {
        tty.c_cflag &= ~CSTOPB;
    }

    switch (config.parity) {
        case Parity::None:
            tty.c_cflag &= ~PARENB;
            break;
        case Parity::Odd:
            tty.c_cflag |= (PARENB | PARODD);
            break;
        case Parity::Even:
            tty.c_cflag |= PARENB;
            tty.c_cflag &= ~PARODD;
            break;
    }
    tty.c_cflag &= ~CRTSCTS;  // no hardware flow control

    // Non-blocking reads; select() provides the timeout.
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 0;

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        std::string err = std::strerror(errno);
        ::close(fd);
        throw SerialError("tcsetattr failed on " + device + ": " + err);
    }

    tcflush(fd, TCIOFLUSH);

    fd_ = fd;
    device_ = device;
}

void SerialPort::close() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

void SerialPort::write_all(const uint8_t* data, size_t len) {
    if (fd_ < 0) throw SerialError("write on closed port");
    size_t written = 0;
    while (written < len) {
        ssize_t n = ::write(fd_, data + written, len - written);
        if (n < 0) {
            if (errno == EINTR) continue;
            throw SerialError(std::string("write failed: ") + std::strerror(errno));
        }
        written += static_cast<size_t>(n);
    }
    // Block until the bytes have actually left the UART.
    tcdrain(fd_);
}

size_t SerialPort::read_some(uint8_t* buffer, size_t max_len, unsigned timeout_ms) {
    if (fd_ < 0) throw SerialError("read on closed port");
    if (max_len == 0) return 0;

    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(fd_, &readfds);

    timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    int sel = select(fd_ + 1, &readfds, nullptr, nullptr, &tv);
    if (sel < 0) {
        if (errno == EINTR) return 0;
        throw SerialError(std::string("select failed: ") + std::strerror(errno));
    }
    if (sel == 0) return 0;  // timeout

    ssize_t n = ::read(fd_, buffer, max_len);
    if (n < 0) {
        if (errno == EINTR || errno == EAGAIN) return 0;
        throw SerialError(std::string("read failed: ") + std::strerror(errno));
    }
    return static_cast<size_t>(n);
}

void SerialPort::flush() {
    if (fd_ >= 0) tcflush(fd_, TCIOFLUSH);
}

std::vector<PortInfo> SerialPort::list_ports() {
    std::vector<PortInfo> ports;

    DIR* dir = ::opendir("/sys/class/tty");
    if (!dir) return ports;  // no sysfs - best-effort, return empty

    for (dirent* ent = ::readdir(dir); ent; ent = ::readdir(dir)) {
        std::string name = ent->d_name;
        if (name == "." || name == "..") continue;

        // A real port has a "device" subdirectory under its tty class entry;
        // virtual lines (tty, console, pts) do not. This filters out the dozens
        // of phantom nodes while keeping ttyUSB*, ttyACM*, real ttyS*, etc.
        struct stat st;
        std::string devlink = "/sys/class/tty/" + name + "/device";
        if (::stat(devlink.c_str(), &st) != 0) continue;

        std::string dev = "/dev/" + name;
        if (::access(dev.c_str(), F_OK) != 0) continue;  // no /dev node

        ports.push_back({dev, describe_tty(name)});
    }
    ::closedir(dir);

    std::sort(ports.begin(), ports.end(),
              [](const PortInfo& a, const PortInfo& b) {
                  return a.device < b.device;
              });
    return ports;
}

}  // namespace fan

#endif  // !_WIN32
