// product_profiles.h - Per-product default parameter sets ("profiles").
//
// A profile is a named bundle of (1) the serial communication settings the
// product normally runs at and (2) the register defaults to program into a fan
// so it is configured consistently for that product. The first profile is the
// e360.
//
// Programming a profile writes each default register over Modbus and, if the
// profile says so, persists them to flash. Communication-setting changes
// (baud / parity / address) only take effect after a power cycle, which the
// tool reports.
#ifndef FAN_TOOL_PRODUCT_PROFILES_H
#define FAN_TOOL_PRODUCT_PROFILES_H

#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

#include "serial_port.h"  // Parity

namespace fan {

// Serial link settings for reaching / running a product.
struct CommSettings {
    unsigned baud = 115200;
    Parity parity = Parity::None;
    uint8_t address = 247;

    // Human-readable form, e.g. "19200 8E1, addr 11".
    std::string str() const;
};

// COPRA factory default communication settings (a brand-new, un-programmed
// unit answers here).
CommSettings factory_default_comm();

// One register to program, expressed in engineering units. `reg_name` must
// match a register in copra_registers().
struct ProfileSetting {
    std::string reg_name;
    double value;
    std::string note;
};

struct ProductProfile {
    std::string name;          // e.g. "e360"
    std::string description;
    CommSettings comm;         // settings the product runs at after programming
    std::vector<ProfileSetting> defaults;
    bool save_to_flash = true; // persist programmed settings
    bool comm_changes = true;  // programming alters baud/parity/address
};

// All known product profiles (e360 is first).
const std::vector<ProductProfile>& product_profiles();

// Look up a profile by name (case-insensitive). Returns nullptr if not found.
const ProductProfile* find_product(const std::string& name);

// Ordered list of comm settings to try when auto-connecting: the factory
// default first, then each product profile's operating settings (deduplicated).
// Extra profiles (e.g. loaded from files) can be appended via `extra`.
std::vector<CommSettings> autoconnect_candidates(
    const std::vector<ProductProfile>& extra = {});

// --- Loading profiles from text files --------------------------------------
//
// Profile file format (one directive per line; '#' starts a comment):
//
//   name        = e360
//   description = e360 plenum fan
//   comm.baud   = 19200
//   comm.parity = even            # none | even | odd
//   comm.address = 11
//   save_to_flash = true
//   set direction     = 9         # set <register> = <value>  [# note]
//   set modbus_address = 11
//
// Parse a profile from a stream. Returns true on success; on failure leaves
// `error` describing the problem (with a line number where possible).
bool parse_profile(std::istream& in, ProductProfile& out, std::string& error);

// Load a profile from a file path. Returns false (with `error` set) if the file
// cannot be opened or fails to parse.
bool load_profile_file(const std::string& path, ProductProfile& out,
                       std::string& error);

// Serialize a profile back to the text-file format (used to export the
// built-in defaults to an editable file).
std::string profile_to_text(const ProductProfile& p);

}  // namespace fan

#endif  // FAN_TOOL_PRODUCT_PROFILES_H
