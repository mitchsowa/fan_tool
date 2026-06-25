// product_profiles.cpp - Product profile definitions and helpers.
#include "product_profiles.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <istream>
#include <sstream>

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

std::vector<CommSettings> autoconnect_candidates(
    const std::vector<ProductProfile>& extra) {
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
    for (const ProductProfile& p : extra) add(p.comm);  // file-loaded profiles
    return out;
}

// --- Profile file parsing --------------------------------------------------

namespace {
// Trim leading/trailing whitespace.
std::string trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

bool parse_parity(const std::string& v, Parity& out) {
    std::string p = to_lower(trim(v));
    if (p == "none" || p == "n") { out = Parity::None; return true; }
    if (p == "even" || p == "e") { out = Parity::Even; return true; }
    if (p == "odd"  || p == "o") { out = Parity::Odd;  return true; }
    return false;
}

bool parse_bool(const std::string& v, bool& out) {
    std::string p = to_lower(trim(v));
    if (p == "true" || p == "1" || p == "yes" || p == "on")  { out = true;  return true; }
    if (p == "false"|| p == "0" || p == "no"  || p == "off") { out = false; return true; }
    return false;
}
}  // namespace

bool parse_profile(std::istream& in, ProductProfile& out, std::string& error) {
    out = ProductProfile{};
    out.comm = factory_default_comm();
    out.save_to_flash = true;
    out.comm_changes = false;
    bool have_name = false;

    std::string raw;
    int ln = 0;
    while (std::getline(in, raw)) {
        ++ln;
        // Strip comments (# ...) and surrounding whitespace.
        size_t hash = raw.find('#');
        std::string line = trim(hash == std::string::npos ? raw : raw.substr(0, hash));
        if (line.empty()) continue;

        size_t eq = line.find('=');
        if (eq == std::string::npos) {
            error = "line " + std::to_string(ln) + ": expected 'key = value'";
            return false;
        }
        std::string key = trim(line.substr(0, eq));
        std::string val = trim(line.substr(eq + 1));
        std::string lkey = to_lower(key);

        if (lkey.rfind("set ", 0) == 0) {
            // Register default: "set <register> = <value>".
            std::string reg = trim(key.substr(4));
            if (reg.empty()) {
                error = "line " + std::to_string(ln) + ": missing register name";
                return false;
            }
            try {
                size_t pos = 0;
                double v = std::stod(val, &pos);
                if (pos != val.size()) throw std::invalid_argument("");
                out.defaults.push_back({reg, v, ""});
            } catch (...) {
                error = "line " + std::to_string(ln) + ": value for '" + reg +
                        "' is not a number";
                return false;
            }
            if (to_lower(reg).rfind("modbus_", 0) == 0) out.comm_changes = true;
        } else if (lkey == "name") {
            out.name = val; have_name = true;
        } else if (lkey == "description") {
            out.description = val;
        } else if (lkey == "comm.baud") {
            try { out.comm.baud = static_cast<unsigned>(std::stoul(val)); }
            catch (...) { error = "line " + std::to_string(ln) + ": bad comm.baud"; return false; }
        } else if (lkey == "comm.parity") {
            if (!parse_parity(val, out.comm.parity)) {
                error = "line " + std::to_string(ln) + ": bad comm.parity (none|even|odd)";
                return false;
            }
        } else if (lkey == "comm.address") {
            try {
                unsigned a = static_cast<unsigned>(std::stoul(val));
                if (a > 247) throw std::out_of_range("");
                out.comm.address = static_cast<uint8_t>(a);
            } catch (...) {
                error = "line " + std::to_string(ln) + ": bad comm.address (0-247)";
                return false;
            }
        } else if (lkey == "save_to_flash") {
            if (!parse_bool(val, out.save_to_flash)) {
                error = "line " + std::to_string(ln) + ": bad save_to_flash (true|false)";
                return false;
            }
        } else if (lkey == "comm_changes") {
            if (!parse_bool(val, out.comm_changes)) {
                error = "line " + std::to_string(ln) + ": bad comm_changes (true|false)";
                return false;
            }
        } else {
            error = "line " + std::to_string(ln) + ": unknown key '" + key + "'";
            return false;
        }
    }

    if (!have_name) { error = "profile is missing a 'name'"; return false; }
    if (out.defaults.empty()) {
        error = "profile '" + out.name + "' defines no 'set' register defaults";
        return false;
    }
    return true;
}

bool load_profile_file(const std::string& path, ProductProfile& out,
                       std::string& error) {
    std::ifstream f(path);
    if (!f) { error = "cannot open profile file '" + path + "'"; return false; }
    return parse_profile(f, out, error);
}

std::string profile_to_text(const ProductProfile& p) {
    std::ostringstream os;
    const char* par = p.comm.parity == Parity::None ? "none"
                      : p.comm.parity == Parity::Even ? "even" : "odd";
    os << "# Product profile: " << p.name << "\n";
    os << "name        = " << p.name << "\n";
    os << "description = " << p.description << "\n";
    os << "comm.baud    = " << p.comm.baud << "\n";
    os << "comm.parity  = " << par << "\n";
    os << "comm.address = " << static_cast<int>(p.comm.address) << "\n";
    os << "save_to_flash = " << (p.save_to_flash ? "true" : "false") << "\n";
    os << "\n# Register defaults: set <register> = <value>\n";
    for (const ProfileSetting& s : p.defaults) {
        os << "set " << s.reg_name << " = " << s.value;
        if (!s.note.empty()) os << "   # " << s.note;
        os << "\n";
    }
    return os.str();
}

}  // namespace fan
