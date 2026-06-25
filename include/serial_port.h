// serial_port.h - Cross-platform serial port abstraction for Modbus RTU.
//
// Provides a thin RAII wrapper around a native serial handle. The same API is
// implemented for POSIX (termios, Linux/macOS) and Windows (Win32 COM). The
// port is always configured for the byte format the COPRA fan expects, with a
// configurable byte format so other devices / baud rates can be used too.
#ifndef FAN_TOOL_SERIAL_PORT_H
#define FAN_TOOL_SERIAL_PORT_H

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace fan {

enum class Parity { None, Odd, Even };

// Configuration for a serial line. Defaults match the COPRA factory settings
// (115200 8N1) per the COPRA Modbus specification.
struct SerialConfig {
    unsigned baud = 115200;
    unsigned data_bits = 8;
    unsigned stop_bits = 1;
    Parity parity = Parity::None;
};

// Thrown for any serial-layer failure (open, configure, read/write error).
class SerialError : public std::runtime_error {
public:
    explicit SerialError(const std::string& what) : std::runtime_error(what) {}
};

class SerialPort {
public:
    SerialPort() = default;
    ~SerialPort();

    // Non-copyable, movable.
    SerialPort(const SerialPort&) = delete;
    SerialPort& operator=(const SerialPort&) = delete;
    SerialPort(SerialPort&& other) noexcept;
    SerialPort& operator=(SerialPort&& other) noexcept;

    // Open and configure the device (e.g. "/dev/ttyUSB0" or "COM3").
    // Throws SerialError on failure.
    void open(const std::string& device, const SerialConfig& config);
    void close();
    bool is_open() const;

    // Write all bytes. Throws SerialError on failure.
    void write_all(const uint8_t* data, size_t len);
    void write_all(const std::vector<uint8_t>& data) {
        write_all(data.data(), data.size());
    }

    // Read up to max_len bytes, blocking until at least one byte arrives or
    // timeout_ms elapses. Returns the number of bytes actually read (0 on
    // timeout). Throws SerialError only on a hard device error.
    size_t read_some(uint8_t* buffer, size_t max_len, unsigned timeout_ms);

    // Discard any buffered input/output (used to resynchronise the bus).
    void flush();

private:
#ifdef _WIN32
    void* handle_ = nullptr;  // HANDLE
#else
    int fd_ = -1;
#endif
    std::string device_;
};

}  // namespace fan

#endif  // FAN_TOOL_SERIAL_PORT_H
