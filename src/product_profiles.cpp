// product_profiles.cpp - Product profile definitions and helpers.
#include "product_profiles.h"

#include <algorithm>
#include <cctype>

namespace fan {

namespace {
std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
}

const char* parity_str(Parity p) {
    switch (p) {
        case Parity::None: return "N";
        case Parity::Odd:  return "O";
        case Parity::Even: return "E";
    }
    return "?";
}
}  // namespace

std::string CommSettings::str() const {
    // e.g. "19200 8E1, addr 11"
    return std::to_string(baud) + " 8" + parity_str(parity) + "1, addr " +
           std::to_string(address);
}

CommSettings factory_default_comm() {
    return CommSettings{115200, Parity::None, 247};
}

const std::vector<ProductProfile>& product_profiles() {
    static const std::vector<ProductProfile> profiles = {
        // -------------------------------------------------------------------
        // e360 - first supported product.
        //
        // These units operate on the RS485 bus at 19200 baud, EVEN parity,
        // Modbus address 11. The communication defaults below program a fan to
        // those settings; the operational defaults set a known-good control
        // configuration. Review/adjust the operational values against the e360
        // application spec before production use.
        // -------------------------------------------------------------------
        ProductProfile{
            "e360",
            "e360 plenum fan - COPRA EC, 19200/8E1, Modbus address 11",
            CommSettings{19200, Parity::Even, 11},
            {
                // --- Communication settings (take effect after power cycle) ---
                {"modbus_baud", 19200, "RS485 bus baud rate for e360"},
                {"modbus_parity", 2, "EVEN parity"},
                {"modbus_stop_bits", 1, "1 stop bit"},
                {"modbus_address", 11, "Modbus follower address for e360"},
                // --- Operational defaults (starting point - verify for e360) ---
                {"direction", 9, "STD/CCW rotation (COPRA supports STD only)"},
                {"modbus_priority", 1, "Modbus is the highest-priority demand source"},
                {"hb_timeout", 20, "Modbus loss (heartbeat) timeout, seconds"},
            },
            /*save_to_flash=*/true,
            /*comm_changes=*/true,
        },
    };
    return profiles;
}

const ProductProfile* find_product(const std::string& name) {
    std::string key = to_lower(name);
    for (const ProductProfile& p : product_profiles()) {
        if (to_lower(p.name) == key) return &p;
    }
    return nullptr;
}

std::vector<CommSettings> autoconnect_candidates() {
    std::vector<CommSettings> out;
    auto add = [&](const CommSettings& c) {
        for (const CommSettings& e : out) {
            if (e.baud == c.baud && e.parity == c.parity &&
                e.address == c.address) {
                return;  // already present
            }
        }
        out.push_back(c);
    };

    add(factory_default_comm());                 // 115200 8N1, addr 247
    for (const ProductProfile& p : product_profiles()) {
        add(p.comm);                             // e.g. e360: 19200 8E1, addr 11
    }
    return out;
}

}  // namespace fan
