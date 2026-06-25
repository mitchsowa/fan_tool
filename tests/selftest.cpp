// selftest.cpp - Hardware-free unit tests for the parts that can be checked
// without a fan attached: the Modbus CRC and the register value codecs.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "copra_registers.h"
#include "modbus_rtu.h"

namespace {
int failures = 0;

void check(bool cond, const char* what) {
    if (cond) {
        std::printf("  ok   : %s\n", what);
    } else {
        std::printf("  FAIL : %s\n", what);
        ++failures;
    }
}

bool approx(double a, double b) { return std::fabs(a - b) < 1e-6; }
}  // namespace

int main() {
    using namespace fan;

    std::printf("CRC-16 (Modbus, poly 0xA001):\n");
    // Classic reference vector: CRC of "123456789" is 0x4B37.
    const uint8_t ref[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    check(modbus_crc16(ref, sizeof(ref)) == 0x4B37, "\"123456789\" -> 0x4B37");

    // A real read-holding request: addr 0x01, FC 0x03, start 0x0000, count 0x000A.
    const uint8_t frame[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x0A};
    check(modbus_crc16(frame, sizeof(frame)) == 0xCDC5,
          "01 03 00 00 00 0A -> 0xCDC5");

    std::printf("Register lookup:\n");
    check(find_register("cmd_speed") != nullptr, "find cmd_speed");
    check(find_register("CMD_SPEED") != nullptr, "case-insensitive lookup");
    check(find_register("speed")->address == 47, "speed -> register 47");
    check(find_register("nope") == nullptr, "unknown register returns null");

    std::printf("Value decode/encode:\n");
    const RegDef* demand = find_register("cmd_demand");  // scale 0.01
    check(approx(decode_value(*demand, 5000), 50.0), "demand raw 5000 -> 50%");
    check(encode_value(*demand, 50.0) == 5000, "demand 50% -> raw 5000");

    const RegDef* speed = find_register("speed");  // s16, scale 1
    check(approx(decode_value(*speed, 0xFFFF), -1.0), "speed 0xFFFF -> -1 RPM");
    check(approx(decode_value(*speed, 2000), 2000.0), "speed 2000 -> 2000 RPM");

    const RegDef* current = find_register("current_a");  // u16, scale 0.01
    check(approx(decode_value(*current, 1234), 12.34), "current 1234 -> 12.34 mA");

    std::printf("Enum / fault decode:\n");
    check(mc_state_name(6) == "RUN", "mcState 6 -> RUN");
    check(mc_state_name(10) == "FAULT_NOW", "mcState 10 -> FAULT_NOW");
    auto faults = decode_faults01(0x0101);  // bit0 + bit8
    check(faults.size() == 2 && faults[0] == "FOC_Duration" &&
              faults[1] == "Over_Current",
          "faults 0x0101 -> FOC_Duration + Over_Current");

    std::printf("\n%s (%d failure%s)\n", failures == 0 ? "ALL PASS" : "FAILED",
                failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
