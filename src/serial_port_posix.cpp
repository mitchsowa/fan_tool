// serial_port_posix.cpp - POSIX (termios) implementation of SerialPort.
// Compiled on non-Windows platforms.
#ifndef _WIN32

#include "serial_port.h"

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <sys/select.h>

#include <cerrno>
#include <cstring>

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

}  // namespace fan

#endif  // !_WIN32
