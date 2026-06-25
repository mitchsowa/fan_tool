// modbus_rtu.cpp - Implementation of the Modbus RTU master.
#include "modbus_rtu.h"

#include <chrono>
#include <cstdio>
#include <thread>

namespace fan {

uint16_t modbus_crc16(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            if (crc & 0x0001) {
                crc >>= 1;
                crc ^= 0xA001;  // reflected polynomial
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

std::string ModbusException::describe(uint8_t function, uint8_t code) {
    const char* meaning;
    switch (code) {
        case 0x01: meaning = "Illegal Function"; break;
        case 0x02: meaning = "Illegal Data Address"; break;
        case 0x03: meaning = "Illegal Data Value"; break;
        case 0x04: meaning = "Client Device Failure"; break;
        case 0x05: meaning = "Acknowledge (deferred/in progress)"; break;
        default:   meaning = "Unknown exception"; break;
    }
    char buf[96];
    std::snprintf(buf, sizeof(buf),
                  "Modbus exception 0x%02X (%s) for function 0x%02X",
                  code, meaning, function);
    return std::string(buf);
}

namespace {
// Append a 16-bit value most-significant-byte first (Modbus big-endian).
void push_u16(std::vector<uint8_t>& v, uint16_t value) {
    v.push_back(static_cast<uint8_t>(value >> 8));
    v.push_back(static_cast<uint8_t>(value & 0xFF));
}
}  // namespace

std::vector<uint16_t> ModbusMaster::read_holding(uint16_t address,
                                                 uint16_t count) {
    return read_registers(0x03, address, count);
}

std::vector<uint16_t> ModbusMaster::read_input(uint16_t address,
                                               uint16_t count) {
    return read_registers(0x04, address, count);
}

std::vector<uint16_t> ModbusMaster::read_registers(uint8_t function,
                                                   uint16_t address,
                                                   uint16_t count) {
    if (count == 0 || count > 125) {
        throw ModbusTransportError("register count out of range (1..125)");
    }
    uint16_t wire_addr = static_cast<uint16_t>(address + address_offset_);

    std::vector<uint8_t> pdu;
    pdu.push_back(function);
    push_u16(pdu, wire_addr);
    push_u16(pdu, count);

    std::vector<uint8_t> payload = transact(pdu, function);

    // payload = [byte_count][data...]
    if (payload.empty()) {
        throw ModbusTransportError("empty read response");
    }
    uint8_t byte_count = payload[0];
    if (byte_count != count * 2 || payload.size() < 1u + byte_count) {
        throw ModbusTransportError("read response length mismatch");
    }

    std::vector<uint16_t> result;
    result.reserve(count);
    for (uint16_t i = 0; i < count; ++i) {
        uint16_t hi = payload[1 + i * 2];
        uint16_t lo = payload[1 + i * 2 + 1];
        result.push_back(static_cast<uint16_t>((hi << 8) | lo));
    }
    return result;
}

void ModbusMaster::write_single(uint16_t address, uint16_t value) {
    uint16_t wire_addr = static_cast<uint16_t>(address + address_offset_);

    std::vector<uint8_t> pdu;
    pdu.push_back(0x06);
    push_u16(pdu, wire_addr);
    push_u16(pdu, value);

    std::vector<uint8_t> payload = transact(pdu, 0x06);
    // Echo of address + value expected (4 bytes).
    if (payload.size() < 4) {
        throw ModbusTransportError("short write-single response");
    }
}

void ModbusMaster::write_multiple(uint16_t address,
                                  const std::vector<uint16_t>& values) {
    if (values.empty() || values.size() > 123) {
        throw ModbusTransportError("write count out of range (1..123)");
    }
    uint16_t wire_addr = static_cast<uint16_t>(address + address_offset_);

    std::vector<uint8_t> pdu;
    pdu.push_back(0x10);
    push_u16(pdu, wire_addr);
    push_u16(pdu, static_cast<uint16_t>(values.size()));
    pdu.push_back(static_cast<uint8_t>(values.size() * 2));
    for (uint16_t v : values) push_u16(pdu, v);

    std::vector<uint8_t> payload = transact(pdu, 0x10);
    if (payload.size() < 4) {
        throw ModbusTransportError("short write-multiple response");
    }
}

std::vector<uint8_t> ModbusMaster::transact(const std::vector<uint8_t>& pdu,
                                            uint8_t function) {
    // Build the full frame: address + PDU + CRC.
    std::vector<uint8_t> frame;
    frame.reserve(pdu.size() + 3);
    frame.push_back(slave_);
    frame.insert(frame.end(), pdu.begin(), pdu.end());
    uint16_t crc = modbus_crc16(frame.data(), frame.size());
    frame.push_back(static_cast<uint8_t>(crc & 0xFF));         // CRC low first
    frame.push_back(static_cast<uint8_t>((crc >> 8) & 0xFF));  // CRC high

    ModbusTransportError last_error("no attempt made");
    for (unsigned attempt = 0; attempt <= retries_; ++attempt) {
        try {
            return transact_once(frame, function);
        } catch (const ModbusTransportError& e) {
            last_error = e;
            // Brief gap, then resync the bus before retrying.
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            port_.flush();
        }
        // ModbusException propagates immediately - it is a definitive answer
        // from the device, not a transport glitch worth retrying.
    }
    throw last_error;
}

std::vector<uint8_t> ModbusMaster::transact_once(
    const std::vector<uint8_t>& frame, uint8_t function) {
    port_.flush();
    port_.write_all(frame);

    // Read the response until it is complete or the timeout expires. We do not
    // know the length up front (it depends on byte count / exception), so we
    // accumulate and re-check after each chunk.
    std::vector<uint8_t> rx;
    uint8_t buf[256];
    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(response_timeout_ms_);

    auto remaining_ms = [&]() -> unsigned {
        auto now = std::chrono::steady_clock::now();
        if (now >= deadline) return 0;
        return static_cast<unsigned>(
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now)
                .count());
    };

    while (true) {
        unsigned needed = 0;  // total bytes expected once header is known
        if (rx.size() >= 2) {
            uint8_t resp_fn = rx[1];
            if (resp_fn & 0x80) {
                needed = 5;  // addr, fn, exc-code, crc-lo, crc-hi
            } else if (function == 0x03 || function == 0x04) {
                // addr, fn, byte_count, data..., crc(2)
                if (rx.size() >= 3) needed = 3 + rx[2] + 2;
            } else {  // 0x06 / 0x10 echo: addr, fn, 4 bytes, crc(2)
                needed = 8;
            }
        }

        if (needed != 0 && rx.size() >= needed) break;

        unsigned timeout = remaining_ms();
        if (timeout == 0) {
            if (rx.empty()) {
                throw ModbusTransportError("response timeout (no reply)");
            }
            throw ModbusTransportError("response timeout (incomplete frame, " +
                                       std::to_string(rx.size()) + " bytes)");
        }

        size_t n = port_.read_some(buf, sizeof(buf), timeout);
        if (n > 0) rx.insert(rx.end(), buf, buf + n);
    }

    // Validate CRC over everything except the trailing CRC pair.
    size_t body = rx.size() - 2;
    uint16_t calc = modbus_crc16(rx.data(), body);
    uint16_t recv = static_cast<uint16_t>(rx[body] | (rx[body + 1] << 8));
    if (calc != recv) {
        throw ModbusTransportError("CRC mismatch in response");
    }

    if (rx[0] != slave_) {
        throw ModbusTransportError("response from wrong slave address");
    }

    uint8_t resp_fn = rx[1];
    if (resp_fn & 0x80) {
        uint8_t code = (rx.size() >= 3) ? rx[2] : 0;
        throw ModbusException(function, code);
    }
    if (resp_fn != function) {
        throw ModbusTransportError("unexpected function code in response");
    }

    // Return the payload between the function byte and the CRC.
    return std::vector<uint8_t>(rx.begin() + 2, rx.begin() + body);
}

}  // namespace fan
