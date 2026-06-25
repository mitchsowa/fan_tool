// fan_controller.h - High-level COPRA fan operations built on ModbusMaster.
//
// Wraps the raw register reads/writes into the operations a commissioning
// engineer thinks in: identify, set speed/demand, start, stop, read status,
// save settings to flash. Shared by both the script runner and the
// interactive shell.
#ifndef FAN_TOOL_FAN_CONTROLLER_H
#define FAN_TOOL_FAN_CONTROLLER_H

#include <cstdint>
#include <string>
#include <vector>

#include "copra_registers.h"
#include "modbus_rtu.h"

namespace fan {

// Snapshot of the live drive metering / status registers.
struct FanStatus {
    uint16_t mc_state_raw = 0;
    std::string mc_state;
    int16_t measured_speed = 0;   // RPM
    int16_t input_power = 0;       // W
    int16_t ipm_temp = 0;          // deg C
    uint16_t bus_voltage = 0;      // V
    double current_a = 0.0;        // A
    uint16_t faults01 = 0;
    uint16_t faults02 = 0;
    std::vector<std::string> active_faults;
    double demand_value = 0.0;     // % currently applied
    uint16_t active_source = 0;
};

struct FanIdentity {
    std::string firmware_version;  // "VV.XX.YY"
    uint16_t app_fw_version = 0;
    uint16_t product_variant = 0;
};

class FanController {
public:
    explicit FanController(ModbusMaster& master) : master_(master) {}

    // --- Identity & status -----------------------------------------------
    FanIdentity identify();
    FanStatus read_status();

    // --- Commissioning commands ------------------------------------------
    // Set target speed in RPM (clears any demand override first).
    void set_speed(uint16_t rpm);
    // Set target demand in percent (0-100); overrides command speed.
    void set_demand(double percent);
    void start();
    void stop();
    void set_direction(bool standard);  // true=STD/CCW, false=REVERSE
    // Force the Modbus channel to be the active demand source.
    void force_modbus_source();

    // Persist current holding-register settings to flash (app + drive).
    // Polls the flash status until complete or throws on error/timeout.
    void save_settings();

    // --- Generic register access -----------------------------------------
    // Read a single register (by definition) and return its engineering value.
    double read_register(const RegDef& reg, uint16_t* raw_out = nullptr);
    // Write an engineering value to a holding register.
    void write_register(const RegDef& reg, double value);

private:
    void save_one(uint16_t cmd_reg, uint16_t status_reg, const char* label);
    ModbusMaster& master_;
};

}  // namespace fan

#endif  // FAN_TOOL_FAN_CONTROLLER_H
