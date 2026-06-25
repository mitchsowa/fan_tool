// fan_controller.cpp - Implementation of high-level fan operations.
#include "fan_controller.h"

#include <chrono>
#include <stdexcept>
#include <thread>

namespace fan {

namespace {
// Decode one ASCII firmware-revision register: each word holds two characters
// (high byte = first). Returns the two characters, skipping NULs.
std::string ascii_word(uint16_t raw) {
    std::string out;
    char hi = static_cast<char>((raw >> 8) & 0xFF);
    char lo = static_cast<char>(raw & 0xFF);
    if (hi >= 0x20 && hi < 0x7F) out.push_back(hi);
    if (lo >= 0x20 && lo < 0x7F) out.push_back(lo);
    return out;
}
}  // namespace

FanIdentity FanController::identify() {
    FanIdentity id;
    // Firmware revision lives in three consecutive ASCII registers 554..556
    // ordered minor, median, major. Read them in one transaction.
    std::vector<uint16_t> fw = master_.read_input(554, 3);
    std::string minor = ascii_word(fw[0]);
    std::string median = ascii_word(fw[1]);
    std::string major = ascii_word(fw[2]);
    id.firmware_version = major + "." + median + "." + minor;

    id.app_fw_version = master_.read_input(35369, 1)[0];
    id.product_variant = master_.read_input(570, 1)[0];
    return id;
}

FanStatus FanController::read_status() {
    FanStatus s;
    // Metering registers 41..52 are contiguous - one read covers most of them.
    std::vector<uint16_t> m = master_.read_input(41, 12);  // 41..52
    s.mc_state_raw = m[0];                       // 41
    s.mc_state = mc_state_name(m[0]);
    s.faults01 = m[1];                           // 42
    s.faults02 = m[2];                           // 43
    s.bus_voltage = m[5];                        // 46
    s.measured_speed = static_cast<int16_t>(m[6]);  // 47
    s.input_power = static_cast<int16_t>(m[8]);  // 49
    s.ipm_temp = static_cast<int16_t>(m[9]);     // 50
    s.current_a = m[10] * 0.01;                  // 51 (scale 0.01 A)
    s.active_faults = decode_faults01(s.faults01);

    // Demand multiplexer status: 34345 demand value, 34346 active source.
    std::vector<uint16_t> d = master_.read_input(34345, 2);
    s.demand_value = d[0] * 0.01;
    s.active_source = d[1];
    return s;
}

void FanController::set_speed(uint16_t rpm) {
    // Command Demand takes priority over Command Speed, so clear it to ensure
    // the speed value is the one that takes effect.
    master_.write_single(reg::kCommandDemand, 0);
    master_.write_single(reg::kCommandSpeed, rpm);
}

void FanController::set_demand(double percent) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    uint16_t raw = static_cast<uint16_t>(percent * 100.0 + 0.5);  // scale 0.01
    master_.write_single(reg::kCommandDemand, raw);
}

void FanController::start() {
    master_.write_single(reg::kStartCommand, 1);
}

void FanController::stop() {
    master_.write_single(reg::kStartCommand, 0);
}

void FanController::set_direction(bool standard) {
    master_.write_single(reg::kDirection,
                         standard ? reg::kDirStd : reg::kDirReverse);
}

void FanController::force_modbus_source() {
    master_.write_single(reg::kDemandSource, reg::kSrcModbus);
}

void FanController::save_settings() {
    save_one(reg::kAppFlashCommand, reg::kAppFlashStatus, "app");
    save_one(reg::kDriveFlashCommand, reg::kDriveFlashStatus, "drive");
}

void FanController::save_one(uint16_t cmd_reg, uint16_t status_reg,
                             const char* label) {
    // Per spec 3.4: issue the save command, then poll status until
    // FLASH_SETTINGS_WRITE_COMPLETE (8) or FLASH_ERROR (2).
    master_.write_single(cmd_reg, reg::kFlashWriteUserSettings);

    auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        uint16_t status = master_.read_input(status_reg, 1)[0];
        if (status == reg::kFlashSettingsWriteComplete) return;
        if (status == reg::kFlashError) {
            throw std::runtime_error(std::string("flash error while saving ") +
                                     label + " settings");
        }
    }
    throw std::runtime_error(std::string("timeout saving ") + label +
                             " settings to flash");
}

double FanController::read_register(const RegDef& reg, uint16_t* raw_out) {
    uint16_t raw = (reg.space == RegSpace::Holding)
                       ? master_.read_holding(reg.address, 1)[0]
                       : master_.read_input(reg.address, 1)[0];
    if (raw_out) *raw_out = raw;
    return decode_value(reg, raw);
}

void FanController::write_register(const RegDef& reg, double value) {
    if (reg.space != RegSpace::Holding) {
        throw std::runtime_error("register '" + reg.name +
                                 "' is read-only (input register)");
    }
    master_.write_single(reg.address, encode_value(reg, value));
}

}  // namespace fan
