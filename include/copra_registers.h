// copra_registers.h - COPRA fan Modbus register map.
//
// Definitions taken from the "COPRA Products Modbus Specification - User
// Registers" (Release 1.0). Only the registers needed for testing and
// commissioning are included; the table is easy to extend.
//
// Scale factor convention (per spec section 4): actual value = raw * scale.
// When writing, raw = round(value / scale).
#ifndef FAN_TOOL_COPRA_REGISTERS_H
#define FAN_TOOL_COPRA_REGISTERS_H

#include <cstdint>
#include <string>
#include <vector>

namespace fan {

enum class RegSpace { Holding, Input };

// Interpretation of the raw 16-bit register word.
enum class RegType {
    U16,       // unsigned, apply scale
    S16,       // signed, apply scale
    Enum,      // unsigned enumeration (scale usually 1)
    BitField,  // bit flags
    Ascii,     // two ASCII characters packed in one word
};

struct RegDef {
    std::string name;     // short script identifier, e.g. "cmd_speed"
    uint16_t address;     // Modbus address from the register map
    RegSpace space;
    RegType type;
    double scale;         // multiply raw by this to get engineering units
    std::string unit;     // e.g. "RPM", "W", "%"
    std::string desc;     // human-readable description
};

// The complete table of known registers.
const std::vector<RegDef>& copra_registers();

// Look up a register by its script name (case-insensitive). Returns nullptr if
// not found.
const RegDef* find_register(const std::string& name);

// Decode a raw register word into an engineering value using the register's
// type and scale.
double decode_value(const RegDef& reg, uint16_t raw);

// Encode an engineering value back into a raw register word for writing.
uint16_t encode_value(const RegDef& reg, double value);

// --- Named constants for the commissioning-critical registers --------------
// (Mirror the register map so callers don't hard-code magic numbers.)
namespace reg {
// Fan control (holding) - register group 3.
constexpr uint16_t kCommandSpeed   = 35945;  // RPM,   0..6000
constexpr uint16_t kCommandDemand   = 35946;  // %,     0..100.00 (raw 0..10000)
constexpr uint16_t kStartCommand    = 35947;  // 1=START, 0=STOP
constexpr uint16_t kDirection       = 35948;  // 9=STD (CCW), 6=REVERSE

// Demand multiplexer (holding) - selects which source drives the fan.
constexpr uint16_t kDemandSource    = 34409;  // 0=priority,5=MODBUS
constexpr uint16_t kModbusPriority  = 34412;  // 1=highest

// Drive metering (input).
constexpr uint16_t kMcState         = 41;     // motor control state
constexpr uint16_t kMcFaults01      = 42;
constexpr uint16_t kMcFaults02      = 43;
constexpr uint16_t kMeasuredSpeed   = 47;     // RPM (s16)
constexpr uint16_t kMeasuredPower   = 49;     // W (s16)
constexpr uint16_t kBusVoltage      = 46;     // V
constexpr uint16_t kIpmTemperature  = 50;     // deg C (s16)

// Flash command registers (holding) and status (input).
constexpr uint16_t kAppFlashCommand = 35689;
constexpr uint16_t kAppFlashStatus  = 35625;
constexpr uint16_t kDriveFlashCommand = 4969;
constexpr uint16_t kDriveFlashStatus  = 4905;

// Flash command values (registers 35689 / 4969). The firmware resets the
// command register back to kFlashWaitingForCmd once it has consumed a command.
constexpr uint16_t kFlashWaitingForCmd     = 0;  // idle / command consumed
constexpr uint16_t kFlashWriteUserSettings = 1;  // RAM2FLASH user settings
constexpr uint16_t kFlashReadUserSettings  = 2;  // FLASH2RAM user settings

// Flash status values (registers 35625 / 4905). The drive races through the
// transient *_COMPLETE codes faster than Modbus polling can sample them and
// settles on a steady healthy state (often FLASH_CRC_VALID = 16). Treat the
// whole healthy group as success and only the explicit error codes as failure.
constexpr uint16_t kFlashOk                    = 0;
constexpr uint16_t kFlashError                 = 2;
constexpr uint16_t kFlashSettingsWriteComplete = 8;
constexpr uint16_t kFlashReadComplete          = 12;
constexpr uint16_t kFlashWriteComplete         = 14;
constexpr uint16_t kFlashWriteCrcComplete      = 15;
constexpr uint16_t kFlashCrcValid              = 16;

// Direction values.
constexpr uint16_t kDirStd     = 9;
constexpr uint16_t kDirReverse = 6;

// Demand source values.
constexpr uint16_t kSrcModbus  = 5;
constexpr uint16_t kSrcPriority = 0;
}  // namespace reg

// True if a flash status code (register 35625 / 4905) indicates a hard failure.
bool flash_status_is_error(uint16_t status);
// True if a flash status code indicates the operation finished successfully
// (any of the *_COMPLETE / OK / CRC_VALID healthy states).
bool flash_status_is_complete(uint16_t status);

// Human-readable decode of the mcState enum (register 41).
std::string mc_state_name(uint16_t value);

// Decode the Fault 01 bitfield (register 42) into a list of active fault names.
std::vector<std::string> decode_faults01(uint16_t value);

}  // namespace fan

#endif  // FAN_TOOL_COPRA_REGISTERS_H
