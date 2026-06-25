// copra_registers.cpp - COPRA register table and value codecs.
#include "copra_registers.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace fan {

namespace {
std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
}
}  // namespace

const std::vector<RegDef>& copra_registers() {
    // Built once on first use. Names are the identifiers used in .fan scripts
    // and the interactive shell.
    static const std::vector<RegDef> table = {
        // --- Fan control (holding) -------------------------------------
        {"cmd_speed", reg::kCommandSpeed, RegSpace::Holding, RegType::U16, 1.0,
         "RPM", "Modbus command speed (0-6000)"},
        {"cmd_demand", reg::kCommandDemand, RegSpace::Holding, RegType::U16,
         0.01, "%", "Modbus command demand (0-100%); overrides cmd_speed"},
        {"start", reg::kStartCommand, RegSpace::Holding, RegType::Enum, 1.0, "",
         "Start/Stop command (1=START, 0=STOP)"},
        {"direction", reg::kDirection, RegSpace::Holding, RegType::Enum, 1.0, "",
         "Direction (9=STD/CCW, 6=REVERSE)"},

        // --- Demand multiplexer (holding) ------------------------------
        {"demand_source", reg::kDemandSource, RegSpace::Holding, RegType::Enum,
         1.0, "", "Demand source (0=priority,1=0-10V,2=4-20mA,3=DI,4=PWM,5=MODBUS)"},
        {"modbus_priority", reg::kModbusPriority, RegSpace::Holding,
         RegType::Enum, 1.0, "", "Modbus demand priority (1=highest)"},
        {"default_demand", 34418, RegSpace::Holding, RegType::U16, 0.01, "%",
         "Default demand used on power up if non-zero"},

        // --- Demand multiplexer (input) --------------------------------
        {"demand_value", 34345, RegSpace::Input, RegType::U16, 0.01, "%",
         "Demand value currently applied to the motor"},
        {"active_source", 34346, RegSpace::Input, RegType::Enum, 1.0, "",
         "Active demand source (1=0-10V,2=4-20mA,3=DI,4=PWM,5=MODBUS)"},

        // --- Drive metering (input) ------------------------------------
        {"mc_state", reg::kMcState, RegSpace::Input, RegType::Enum, 1.0, "",
         "Motor control state (0=IDLE,4=START,6=RUN,8=STOP,10=FAULT)"},
        {"faults1", reg::kMcFaults01, RegSpace::Input, RegType::BitField, 1.0, "",
         "Active faults bitfield 01"},
        {"faults2", reg::kMcFaults02, RegSpace::Input, RegType::BitField, 1.0, "",
         "Active safety-core faults bitfield 02"},
        {"app_state", 44, RegSpace::Input, RegType::Enum, 1.0, "",
         "Application state machine state"},
        {"act_direction", 45, RegSpace::Input, RegType::Enum, 1.0, "",
         "Actual motor direction (9=STD, 6=REVERSE)"},
        {"bus_voltage", reg::kBusVoltage, RegSpace::Input, RegType::U16, 1.0,
         "V", "DC bus voltage"},
        {"speed", reg::kMeasuredSpeed, RegSpace::Input, RegType::S16, 1.0, "RPM",
         "Measured speed feedback"},
        {"torque", 48, RegSpace::Input, RegType::S16, 1.0, "pu",
         "Measured torque feedback"},
        {"power", reg::kMeasuredPower, RegSpace::Input, RegType::S16, 1.0, "W",
         "Calculated input power"},
        {"shaft_power", 65, RegSpace::Input, RegType::S16, 1.0, "W",
         "Calculated shaft power"},
        {"ipm_temp", reg::kIpmTemperature, RegSpace::Input, RegType::S16, 1.0,
         "degC", "Control IPM temperature"},
        {"current_a", 51, RegSpace::Input, RegType::U16, 0.01, "A",
         "Peak motor phase current Ia"},
        {"current_b", 52, RegSpace::Input, RegType::U16, 0.01, "A",
         "Peak motor phase current Ib"},

        // --- Identity (input) ------------------------------------------
        {"fw_minor", 554, RegSpace::Input, RegType::Ascii, 1.0, "",
         "Drive firmware revision - minor (YY)"},
        {"fw_median", 555, RegSpace::Input, RegType::Ascii, 1.0, "",
         "Drive firmware revision - median (XX)"},
        {"fw_major", 556, RegSpace::Input, RegType::Ascii, 1.0, "",
         "Drive firmware revision - major (VV)"},
        {"app_fw_version", 35369, RegSpace::Input, RegType::U16, 1.0, "",
         "Application firmware version"},
        {"product_variant", 570, RegSpace::Input, RegType::U16, 1.0, "",
         "Product variant code"},

        // --- Modbus settings (holding) ---------------------------------
        {"modbus_baud", 33897, RegSpace::Holding, RegType::Enum, 100.0, "baud",
         "Modbus baud rate (raw*100; e.g. 1152=115200). Applied on power cycle"},
        {"modbus_address", 33902, RegSpace::Holding, RegType::U16, 1.0, "",
         "Unit Modbus address (1-247). Applied on power cycle"},
        {"modbus_parity", 33900, RegSpace::Holding, RegType::Enum, 1.0, "",
         "Modbus parity (0=NONE,1=ODD,2=EVEN). Applied on power cycle"},
        {"modbus_stop_bits", 33899, RegSpace::Holding, RegType::Enum, 1.0, "",
         "Modbus stop bits (1 or 2). Applied on power cycle"},
        {"hb_timeout", 33905, RegSpace::Holding, RegType::U16, 1.0, "s",
         "Modbus heartbeat (loss) timeout"},
        {"loss_demand", 33906, RegSpace::Holding, RegType::U16, 0.01, "%",
         "Demand used when Modbus loss is detected"},

        // --- Flash (holding command / input status) --------------------
        {"app_flash_cmd", reg::kAppFlashCommand, RegSpace::Holding,
         RegType::Enum, 1.0, "", "App flash command (1=save user settings)"},
        {"app_flash_status", reg::kAppFlashStatus, RegSpace::Input,
         RegType::Enum, 1.0, "", "App flash status (8=write complete,2=error)"},
        {"drive_flash_cmd", reg::kDriveFlashCommand, RegSpace::Holding,
         RegType::Enum, 1.0, "", "Drive flash command (1=save user settings)"},
        {"drive_flash_status", reg::kDriveFlashStatus, RegSpace::Input,
         RegType::Enum, 1.0, "", "Drive flash status (8=write complete,2=error)"},
    };
    return table;
}

const RegDef* find_register(const std::string& name) {
    std::string key = to_lower(name);
    for (const RegDef& r : copra_registers()) {
        if (to_lower(r.name) == key) return &r;
    }
    return nullptr;
}

double decode_value(const RegDef& reg, uint16_t raw) {
    switch (reg.type) {
        case RegType::S16:
            return static_cast<int16_t>(raw) * reg.scale;
        case RegType::U16:
        case RegType::Enum:
        case RegType::BitField:
        case RegType::Ascii:
        default:
            return raw * reg.scale;
    }
}

uint16_t encode_value(const RegDef& reg, double value) {
    double raw = value / reg.scale;
    // Round to nearest; clamp into the 16-bit range.
    long rounded = std::lround(raw);
    if (reg.type == RegType::S16) {
        if (rounded < -32768) rounded = -32768;
        if (rounded > 32767) rounded = 32767;
        return static_cast<uint16_t>(static_cast<int16_t>(rounded));
    }
    if (rounded < 0) rounded = 0;
    if (rounded > 65535) rounded = 65535;
    return static_cast<uint16_t>(rounded);
}

std::string mc_state_name(uint16_t value) {
    switch (value) {
        case 0:  return "IDLE";
        case 4:  return "START";
        case 6:  return "RUN";
        case 8:  return "STOP";
        case 9:  return "STOP_IDLE";
        case 10: return "FAULT_NOW";
        case 11: return "FAULT_OVER";
        default: return "UNKNOWN(" + std::to_string(value) + ")";
    }
}

std::vector<std::string> decode_faults01(uint16_t value) {
    static const char* names[] = {
        "FOC_Duration",        "Under_Voltage",      "Over_Voltage",
        "Over_Temperature",    "Speed_Feedback",     "Startup",
        "Input_Loss_of_Phase", "Output_Loss_of_Phase", "Over_Current",
        "Safety_Core",         "Internal_Comm_Loss", "Software_Error",
    };
    std::vector<std::string> active;
    for (int bit = 0; bit < 12; ++bit) {
        if (value & (1u << bit)) active.emplace_back(names[bit]);
    }
    return active;
}

}  // namespace fan
