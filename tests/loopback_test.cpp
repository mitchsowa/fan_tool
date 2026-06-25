// loopback_test.cpp - End-to-end Modbus RTU test over a real PTY pair.
//
// Spawns a mock COPRA slave on the master side of a pseudo-terminal and drives
// it with ModbusMaster (through a real SerialPort on the slave side). This
// exercises the complete path - frame construction, CRC, serial I/O, response
// accumulation and parsing - without any physical fan. POSIX only.
#ifndef _WIN32

#include <poll.h>
#include <pty.h>
#include <unistd.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <sstream>
#include <thread>
#include <vector>

#include "command_interpreter.h"
#include "modbus_rtu.h"
#include "serial_port.h"

namespace {
int failures = 0;
void check(bool cond, const char* what) {
    std::printf("  %s : %s\n", cond ? "ok  " : "FAIL", what);
    if (!cond) ++failures;
}

uint16_t crc(const std::vector<uint8_t>& d, size_t len) {
    return fan::modbus_crc16(d.data(), len);
}

// Minimal COPRA-like slave. Holds register 41 = 6 (RUN) for input reads and
// echoes writes. Runs until `stop` is set, reading raw bytes from `fd`.
struct MockSlave {
    int fd;
    uint8_t address;
    std::atomic<bool>& stop;
    uint16_t last_written_value = 0;
    uint16_t last_written_addr = 0;
    std::map<uint16_t, uint16_t> writes{};  // every FC06 write (addr -> value)

    void run() {
        std::vector<uint8_t> rx;
        uint8_t buf[256];
        while (!stop.load()) {
            pollfd pfd{fd, POLLIN, 0};
            int pr = ::poll(&pfd, 1, 50);  // 50 ms - lets us re-check `stop`
            if (pr <= 0) continue;
            ssize_t n = ::read(fd, buf, sizeof(buf));
            if (n <= 0) continue;
            rx.insert(rx.end(), buf, buf + n);
            // Try to consume complete requests from the buffer.
            while (rx.size() >= 4) {
                size_t consumed = try_handle(rx);
                if (consumed == 0) break;  // need more bytes
                rx.erase(rx.begin(), rx.begin() + consumed);
            }
        }
    }

    // Returns number of bytes consumed (0 if frame incomplete).
    size_t try_handle(std::vector<uint8_t>& rx) {
        uint8_t fn = rx[1];
        size_t need = 0;
        if (fn == 0x03 || fn == 0x04 || fn == 0x06) {
            need = 8;
        } else if (fn == 0x10) {
            if (rx.size() < 7) return 0;
            need = 9 + rx[6];
        } else {
            return 1;  // skip unknown byte
        }
        if (rx.size() < need) return 0;

        // Verify request CRC; if bad, drop one byte and resync.
        uint16_t want = static_cast<uint16_t>(rx[need - 2] | (rx[need - 1] << 8));
        if (crc(rx, need - 2) != want) return 1;
        if (rx[0] != address) return need;  // not for us; ignore

        uint16_t start = static_cast<uint16_t>((rx[2] << 8) | rx[3]);
        if (fn == 0x03 || fn == 0x04) {
            uint16_t count = static_cast<uint16_t>((rx[4] << 8) | rx[5]);
            send_read_reply(fn, start, count);
        } else if (fn == 0x06) {
            uint16_t val = static_cast<uint16_t>((rx[4] << 8) | rx[5]);
            last_written_addr = start;
            last_written_value = val;
            writes[start] = val;
            send_echo(rx, need);  // FC06 response echoes the request
        } else if (fn == 0x10) {
            send_write_multi_reply(start, (rx[4] << 8) | rx[5]);
        }
        return need;
    }

    void send_frame(std::vector<uint8_t> body) {
        uint16_t c = crc(body, body.size());
        body.push_back(static_cast<uint8_t>(c & 0xFF));
        body.push_back(static_cast<uint8_t>((c >> 8) & 0xFF));
        (void)!::write(fd, body.data(), body.size());
    }

    void send_read_reply(uint8_t fn, uint16_t start, uint16_t count) {
        std::vector<uint8_t> body = {address, fn,
                                     static_cast<uint8_t>(count * 2)};
        for (uint16_t i = 0; i < count; ++i) {
            uint16_t reg = start + i;
            uint16_t value = reg;        // default: echo the address
            if (reg == 41) value = 6;    // mcState = RUN
            if (reg == 47) value = 0xF830;  // measuredSpeed = -2000 (s16)
            if (reg == 35625 || reg == 4905) value = 8;  // flash write complete
            body.push_back(static_cast<uint8_t>(value >> 8));
            body.push_back(static_cast<uint8_t>(value & 0xFF));
        }
        send_frame(body);
    }

    void send_echo(const std::vector<uint8_t>& rx, size_t need) {
        std::vector<uint8_t> body(rx.begin(), rx.begin() + (need - 2));
        send_frame(body);
    }

    void send_write_multi_reply(uint16_t start, uint16_t count) {
        std::vector<uint8_t> body = {address, 0x10,
                                     static_cast<uint8_t>(start >> 8),
                                     static_cast<uint8_t>(start & 0xFF),
                                     static_cast<uint8_t>(count >> 8),
                                     static_cast<uint8_t>(count & 0xFF)};
        send_frame(body);
    }
};

}  // namespace

int main() {
    using namespace fan;
    std::printf("Modbus RTU loopback over PTY:\n");

    int master_fd, slave_fd;
    char slave_name[256];
    if (openpty(&master_fd, &slave_fd, slave_name, nullptr, nullptr) != 0) {
        std::printf("  SKIP : openpty unavailable\n");
        return 0;
    }
    // The SerialPort opens the slave by name; close our own slave fd so only it
    // holds the slave end.
    ::close(slave_fd);

    std::atomic<bool> stop{false};
    MockSlave slave{master_fd, 247, stop};
    std::thread slave_thread([&] { slave.run(); });

    int rc = 0;
    try {
        SerialPort port;
        SerialConfig cfg;  // 115200 8N1
        port.open(slave_name, cfg);

        ModbusMaster m(port);
        m.set_slave_address(247);
        m.set_response_timeout_ms(500);

        // Read input register 41 (mcState) -> expect 6 (RUN).
        auto r1 = m.read_input(41, 1);
        check(r1.size() == 1 && r1[0] == 6, "read_input(41) == 6 (RUN)");

        // Read measured speed (reg 47) and interpret as signed -2000.
        auto r2 = m.read_input(47, 1);
        check(r2.size() == 1 && static_cast<int16_t>(r2[0]) == -2000,
              "read_input(47) == -2000 RPM (signed)");

        // Multi-register read 41..43.
        auto r3 = m.read_input(41, 3);
        check(r3.size() == 3 && r3[0] == 6 && r3[2] == 43,
              "read_input(41,3) returns 3 registers");

        // Write single holding register and confirm the slave saw it.
        m.write_single(35945, 1500);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        check(slave.last_written_addr == 35945 && slave.last_written_value == 1500,
              "write_single(35945, 1500) reached slave");

        // Write multiple holding registers.
        m.write_multiple(35945, {1000, 0, 1});
        check(true, "write_multiple completed without error");

        port.close();

        // End-to-end: drive the CommandInterpreter to program the e360 profile
        // through the same mock. Verifies the comm registers are written with
        // the right raw values and the flash-save poll completes.
        std::ostringstream sink;
        CommandInterpreter interp(sink, /*interactive=*/false);
        interp.set_default_port(slave_name);
        interp.set_default_address(247);
        interp.execute("connect");
        CommandResult pr = interp.execute("program e360");
        check(pr.ok, "program e360 completed (interpreter)");
        check(slave.writes[33902] == 11, "e360 set modbus_address (33902) = 11");
        check(slave.writes[33897] == 192, "e360 set modbus_baud (33897) raw = 192 (19200)");
        check(slave.writes[33900] == 2, "e360 set modbus_parity (33900) = 2 (EVEN)");
        check(slave.writes[35948] == 9, "e360 set direction (35948) = 9 (STD)");
        interp.execute("disconnect");

    } catch (const std::exception& e) {
        std::printf("  FAIL : exception: %s\n", e.what());
        ++failures;
        rc = 1;
    }

    stop.store(true);
    // Nudge the blocking read in the slave thread.
    (void)!::write(master_fd, "", 0);
    ::close(master_fd);
    slave_thread.join();

    std::printf("\n%s (%d failure%s)\n", failures == 0 ? "ALL PASS" : "FAILED",
                failures, failures == 1 ? "" : "s");
    return (failures == 0 && rc == 0) ? 0 : 1;
}

#else
int main() { return 0; }  // Windows: PTY loopback not applicable
#endif
