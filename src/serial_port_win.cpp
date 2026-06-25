// serial_port_win.cpp - Win32 implementation of SerialPort.
// Compiled only on Windows.
#ifdef _WIN32

#include "serial_port.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>

namespace fan {

namespace {
// Windows needs the "\\.\COMx" prefix to open ports numbered 10 and above; the
// prefix is harmless for low-numbered ports too.
std::string make_device_path(const std::string& device) {
    if (device.rfind("\\\\.\\", 0) == 0) return device;  // already prefixed
    return std::string("\\\\.\\") + device;
}
}  // namespace

SerialPort::~SerialPort() { close(); }

SerialPort::SerialPort(SerialPort&& other) noexcept
    : handle_(other.handle_), device_(std::move(other.device_)) {
    other.handle_ = nullptr;
}

SerialPort& SerialPort::operator=(SerialPort&& other) noexcept {
    if (this != &other) {
        close();
        handle_ = other.handle_;
        device_ = std::move(other.device_);
        other.handle_ = nullptr;
    }
    return *this;
}

bool SerialPort::is_open() const {
    return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE;
}

void SerialPort::open(const std::string& device, const SerialConfig& config) {
    close();

    std::string path = make_device_path(device);
    HANDLE h = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                           OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        throw SerialError("cannot open " + device + " (error " +
                          std::to_string(GetLastError()) + ")");
    }

    DCB dcb{};
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(h, &dcb)) {
        CloseHandle(h);
        throw SerialError("GetCommState failed on " + device);
    }

    dcb.BaudRate = config.baud;
    dcb.ByteSize = static_cast<BYTE>(config.data_bits);
    dcb.StopBits = (config.stop_bits == 2) ? TWOSTOPBITS : ONESTOPBIT;
    switch (config.parity) {
        case Parity::None: dcb.Parity = NOPARITY;   dcb.fParity = FALSE; break;
        case Parity::Odd:  dcb.Parity = ODDPARITY;  dcb.fParity = TRUE;  break;
        case Parity::Even: dcb.Parity = EVENPARITY; dcb.fParity = TRUE;  break;
    }
    dcb.fBinary = TRUE;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fDtrControl = DTR_CONTROL_DISABLE;
    dcb.fRtsControl = RTS_CONTROL_DISABLE;
    dcb.fOutX = FALSE;
    dcb.fInX = FALSE;

    if (!SetCommState(h, &dcb)) {
        CloseHandle(h);
        throw SerialError("SetCommState failed on " + device);
    }

    // Per-read timeouts are applied dynamically in read_some(); set sane
    // defaults here so a stray read never blocks forever.
    COMMTIMEOUTS timeouts{};
    timeouts.ReadIntervalTimeout = MAXDWORD;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    timeouts.ReadTotalTimeoutConstant = 100;
    timeouts.WriteTotalTimeoutMultiplier = 0;
    timeouts.WriteTotalTimeoutConstant = 500;
    SetCommTimeouts(h, &timeouts);

    PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR);

    handle_ = h;
    device_ = device;
}

void SerialPort::close() {
    if (is_open()) {
        CloseHandle(handle_);
    }
    handle_ = nullptr;
}

void SerialPort::write_all(const uint8_t* data, size_t len) {
    if (!is_open()) throw SerialError("write on closed port");
    size_t written = 0;
    while (written < len) {
        DWORD n = 0;
        if (!WriteFile(handle_, data + written,
                       static_cast<DWORD>(len - written), &n, nullptr)) {
            throw SerialError("write failed (error " +
                              std::to_string(GetLastError()) + ")");
        }
        written += n;
    }
    FlushFileBuffers(handle_);
}

size_t SerialPort::read_some(uint8_t* buffer, size_t max_len, unsigned timeout_ms) {
    if (!is_open()) throw SerialError("read on closed port");
    if (max_len == 0) return 0;

    // Apply the caller's timeout: return as soon as any byte is available, but
    // wait at most timeout_ms for the first byte.
    COMMTIMEOUTS timeouts{};
    timeouts.ReadIntervalTimeout = MAXDWORD;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    timeouts.ReadTotalTimeoutConstant = timeout_ms;
    SetCommTimeouts(handle_, &timeouts);

    DWORD n = 0;
    if (!ReadFile(handle_, buffer, static_cast<DWORD>(max_len), &n, nullptr)) {
        throw SerialError("read failed (error " +
                          std::to_string(GetLastError()) + ")");
    }
    return n;
}

void SerialPort::flush() {
    if (is_open()) PurgeComm(handle_, PURGE_RXCLEAR | PURGE_TXCLEAR);
}

std::vector<PortInfo> SerialPort::list_ports() {
    std::vector<PortInfo> ports;

    // The kernel publishes every present COM port under this registry key:
    // each value's name is the device path and its data is the "COMx" name.
    HKEY key;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                      "HARDWARE\\DEVICEMAP\\SERIALCOMM", 0, KEY_READ,
                      &key) != ERROR_SUCCESS) {
        return ports;  // key absent when no ports exist - best-effort
    }

    char value_name[256];
    BYTE data[256];
    for (DWORD i = 0;; ++i) {
        DWORD name_len = sizeof(value_name);
        DWORD data_len = sizeof(data);
        DWORD type = 0;
        LONG r = RegEnumValueA(key, i, value_name, &name_len, nullptr, &type,
                               data, &data_len);
        if (r == ERROR_NO_MORE_ITEMS) break;
        if (r != ERROR_SUCCESS) continue;
        if (type != REG_SZ) continue;

        std::string com(reinterpret_cast<char*>(data));
        // The value name is the underlying device (e.g. "\Device\Serial0" or a
        // USB-serial node) - useful as a description.
        ports.push_back({com, std::string(value_name, name_len)});
    }
    RegCloseKey(key);

    std::sort(ports.begin(), ports.end(),
              [](const PortInfo& a, const PortInfo& b) {
                  return a.device < b.device;
              });
    return ports;
}

}  // namespace fan

#endif  // _WIN32
