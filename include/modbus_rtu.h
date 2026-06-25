// modbus_rtu.h - Minimal Modbus RTU master for the COPRA fan.
//
// Implements the four function codes the COPRA products support
// (per the COPRA Modbus Specification, section 2.6):
//   0x03 Read Holding Registers
//   0x04 Read Input Registers
//   0x06 Write Single Holding Register
//   0x10 Write Multiple Holding Registers
//
// Framing, CRC-16 (poly 0xA001, init 0xFFFF), exception decoding and bounded
// retries are handled here. Register addresses are passed as they appear in the
// COPRA register map; an adjustable address offset (default 0) handles the
// classic "register number vs. zero-based address" ambiguity if needed.
#ifndef FAN_TOOL_MODBUS_RTU_H
#define FAN_TOOL_MODBUS_RTU_H

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "serial_port.h"

namespace fan {

// Compute the Modbus RTU CRC-16 over a byte buffer. Exposed for testing.
uint16_t modbus_crc16(const uint8_t* data, size_t len);

// Raised when the device replies with a Modbus exception response (FC|0x80).
class ModbusException : public std::runtime_error {
public:
    ModbusException(uint8_t function, uint8_t code)
        : std::runtime_error(describe(function, code)),
          function_(function), code_(code) {}

    uint8_t function() const { return function_; }
    uint8_t code() const { return code_; }

private:
    static std::string describe(uint8_t function, uint8_t code);
    uint8_t function_;
    uint8_t code_;
};

// Raised for transport-level problems: timeout, bad CRC, malformed reply.
class ModbusTransportError : public std::runtime_error {
public:
    explicit ModbusTransportError(const std::string& what)
        : std::runtime_error(what) {}
};

class ModbusMaster {
public:
    // The master borrows the serial port; the caller owns it and keeps it open.
    explicit ModbusMaster(SerialPort& port) : port_(port) {}

    void set_slave_address(uint8_t address) { slave_ = address; }
    uint8_t slave_address() const { return slave_; }

    // Offset added to every register address before it goes on the wire.
    // 0 (default) sends map addresses verbatim; -1 if a unit expects the
    // "register number minus one" convention.
    void set_address_offset(int offset) { address_offset_ = offset; }
    int address_offset() const { return address_offset_; }

    void set_response_timeout_ms(unsigned ms) { response_timeout_ms_ = ms; }
    void set_retries(unsigned retries) { retries_ = retries; }

    // FC 0x03: read `count` holding registers starting at `address`.
    std::vector<uint16_t> read_holding(uint16_t address, uint16_t count);
    // FC 0x04: read `count` input registers starting at `address`.
    std::vector<uint16_t> read_input(uint16_t address, uint16_t count);
    // FC 0x06: write a single holding register.
    void write_single(uint16_t address, uint16_t value);
    // FC 0x10: write multiple consecutive holding registers.
    void write_multiple(uint16_t address, const std::vector<uint16_t>& values);

private:
    std::vector<uint16_t> read_registers(uint8_t function, uint16_t address,
                                          uint16_t count);
    // Send one request frame and return the validated response payload (the
    // bytes after address+function, excluding CRC). Performs retries.
    std::vector<uint8_t> transact(const std::vector<uint8_t>& pdu,
                                  uint8_t function);
    std::vector<uint8_t> transact_once(const std::vector<uint8_t>& frame,
                                       uint8_t function);

    SerialPort& port_;
    uint8_t slave_ = 247;             // COPRA default follower address
    int address_offset_ = 0;
    unsigned response_timeout_ms_ = 600;  // > spec max response time (500 ms)
    unsigned retries_ = 2;
};

}  // namespace fan

#endif  // FAN_TOOL_MODBUS_RTU_H
