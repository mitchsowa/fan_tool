// command_interpreter.cpp - Implementation of the command interpreter.
#include "command_interpreter.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <thread>
#include <vector>

namespace fan {

namespace {

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
}

// Split a line into whitespace-separated tokens.
std::vector<std::string> tokenize(const std::string& line) {
    std::vector<std::string> tokens;
    std::istringstream iss(line);
    std::string tok;
    while (iss >> tok) tokens.push_back(tok);
    return tokens;
}

// Strip a trailing inline comment (# or //) and surrounding whitespace.
std::string strip_comment(const std::string& line) {
    std::string result = line;
    size_t hash = result.find('#');
    size_t slashes = result.find("//");
    size_t cut = std::min(hash, slashes);
    if (cut != std::string::npos) result = result.substr(0, cut);
    size_t end = result.find_last_not_of(" \t\r\n");
    if (end == std::string::npos) return "";
    size_t begin = result.find_first_not_of(" \t\r\n");
    return result.substr(begin, end - begin + 1);
}

bool parse_double(const std::string& s, double& out) {
    try {
        size_t pos = 0;
        out = std::stod(s, &pos);
        return pos == s.size();
    } catch (...) {
        return false;
    }
}

bool parse_long(const std::string& s, long& out) {
    try {
        size_t pos = 0;
        out = std::stol(s, &pos, 0);  // base 0 -> accepts 0x.. too
        return pos == s.size();
    } catch (...) {
        return false;
    }
}

}  // namespace

CommandInterpreter::CommandInterpreter(std::ostream& out, bool interactive)
    : out_(out), interactive_(interactive), master_(port_),
      controller_(master_) {}

void CommandInterpreter::apply_master_config() {
    master_.set_slave_address(slave_);
    master_.set_address_offset(address_offset_);
    master_.set_response_timeout_ms(response_timeout_ms_);
    master_.set_retries(retries_);
}

void CommandInterpreter::ensure_connected() {
    if (!connected_) {
        throw std::runtime_error("not connected - use 'connect' first");
    }
}

void CommandInterpreter::cmd_connect() {
    if (port_name_.empty()) {
        throw std::runtime_error("no serial port set - use 'port <device>'");
    }
    if (connected_) port_.close();

    SerialConfig cfg;
    cfg.baud = baud_;
    cfg.parity = parity_;
    port_.open(port_name_, cfg);
    apply_master_config();
    connected_ = true;
    out_ << "Connected to " << port_name_ << " @ " << baud_ << " 8"
         << (parity_ == Parity::None ? "N" : parity_ == Parity::Even ? "E" : "O")
         << "1, slave address " << static_cast<int>(slave_) << "\n";
}

bool CommandInterpreter::try_connect(const CommSettings& comm) {
    if (port_name_.empty()) {
        throw std::runtime_error("no serial port set - use 'port <device>'");
    }
    port_.close();
    connected_ = false;
    try {
        SerialConfig cfg;
        cfg.baud = comm.baud;
        cfg.parity = comm.parity;
        port_.open(port_name_, cfg);
        // Probe with short timing so a wrong setting fails fast.
        master_.set_slave_address(comm.address);
        master_.set_address_offset(address_offset_);
        master_.set_response_timeout_ms(300);
        master_.set_retries(1);
        master_.read_input(reg::kMcState, 1);  // throws if no valid reply
    } catch (const std::exception&) {
        port_.close();
        master_.set_response_timeout_ms(response_timeout_ms_);
        master_.set_retries(retries_);
        return false;
    }
    // Probe succeeded - adopt these settings as the live connection.
    baud_ = comm.baud;
    parity_ = comm.parity;
    slave_ = comm.address;
    connected_ = true;
    master_.set_response_timeout_ms(response_timeout_ms_);
    master_.set_retries(retries_);
    return true;
}

void CommandInterpreter::cmd_autoconnect() {
    out_ << "Auto-connecting on " << port_name_ << " ...\n";
    // Try the currently-configured settings first, then the known candidates
    // (factory default, then each product profile's operating settings),
    // skipping duplicates.
    std::vector<CommSettings> tries;
    auto add = [&](const CommSettings& c) {
        for (const CommSettings& e : tries)
            if (e.baud == c.baud && e.parity == c.parity && e.address == c.address)
                return;
        tries.push_back(c);
    };
    add(CommSettings{baud_, parity_, slave_});
    for (const CommSettings& c : autoconnect_candidates(loaded_profiles_)) add(c);

    for (const CommSettings& c : tries) {
        out_ << "  trying " << c.str() << " ... ";
        if (try_connect(c)) {
            out_ << "OK\n";
            out_ << "Connected at " << c.str() << "\n";
            return;
        }
        out_ << "no response\n";
    }
    throw std::runtime_error(
        "auto-connect failed: no fan responded on any known comm setting");
}

void CommandInterpreter::cmd_list_products() {
    out_ << "Available product profiles:\n";
    for (const ProductProfile& p : loaded_profiles_) {
        out_ << "  " << p.name << "  - " << p.description << "   [loaded from file]\n";
        out_ << "      comm after programming: " << p.comm.str() << ", "
             << p.defaults.size() << " register(s)\n";
    }
    for (const ProductProfile& p : product_profiles()) {
        // Skip a built-in that a loaded profile of the same name shadows.
        bool shadowed = false;
        for (const ProductProfile& l : loaded_profiles_)
            if (l.name == p.name) shadowed = true;
        if (shadowed) continue;
        out_ << "  " << p.name << "  - " << p.description << "   [built-in]\n";
        out_ << "      comm after programming: " << p.comm.str() << ", "
             << p.defaults.size() << " register(s)"
             << (p.save_to_flash ? ", saved to flash" : "") << "\n";
    }
    out_ << "Load more with 'loadprofile <file>'.\n";
}

void CommandInterpreter::cmd_list_serial_ports() {
    std::vector<PortInfo> ports = SerialPort::list_ports();
    if (ports.empty()) {
        out_ << "No serial ports found.\n";
        return;
    }
    out_ << "Available serial ports:\n";
    for (const PortInfo& p : ports) {
        out_ << "  " << p.device;
        if (!p.description.empty()) out_ << "  - " << p.description;
        if (p.device == port_name_) out_ << "   [current]";
        out_ << "\n";
    }
    out_ << "Select one with 'port <device>'.\n";
}

bool CommandInterpreter::resolve_profile(const std::string& name,
                                         ProductProfile& out) {
    // 1) Already loaded from a file this session.
    for (const ProductProfile& p : loaded_profiles_) {
        if (p.name == name) { out = p; return true; }
    }
    // 2) A matching profile file on disk.
    std::vector<std::string> candidates;
    if (name.find('/') != std::string::npos ||
        name.size() > 8 /* maybe ends in .profile */) {
        candidates.push_back(name);
    }
    candidates.push_back(name + ".profile");
    candidates.push_back("profiles/" + name + ".profile");
    for (const std::string& path : candidates) {
        ProductProfile p;
        std::string err;
        if (load_profile_file(path, p, err)) {
            out_ << "  (loaded profile from " << path << ")\n";
            loaded_profiles_.push_back(p);  // cache for the session
            out = p;
            return true;
        }
    }
    // 3) Compiled-in default.
    const ProductProfile* b = find_product(name);
    if (b) { out = *b; return true; }
    return false;
}

void CommandInterpreter::cmd_load_profile(const std::string& path) {
    ProductProfile p;
    std::string err;
    if (!load_profile_file(path, p, err)) {
        throw std::runtime_error("profile load failed: " + err);
    }
    // Warn (but don't fail) on register names that won't resolve when programmed.
    int unknown = 0;
    for (const ProfileSetting& s : p.defaults) {
        const RegDef* reg = find_register(s.reg_name);
        if (!reg) { out_ << "  WARNING: unknown register '" << s.reg_name
                         << "'\n"; ++unknown; }
        else if (reg->space != RegSpace::Holding)
            out_ << "  WARNING: '" << s.reg_name << "' is read-only\n";
    }
    // Replace any existing profile of the same name.
    loaded_profiles_.erase(
        std::remove_if(loaded_profiles_.begin(), loaded_profiles_.end(),
                       [&](const ProductProfile& e) { return e.name == p.name; }),
        loaded_profiles_.end());
    loaded_profiles_.push_back(p);
    out_ << "Loaded profile '" << p.name << "' (" << p.defaults.size()
         << " register(s), comm " << p.comm.str() << ")"
         << (unknown ? " with warnings" : "") << "\n";
}

void CommandInterpreter::cmd_program(const std::string& product) {
    ensure_connected();
    ProductProfile p;
    if (!resolve_profile(product, p)) {
        throw std::runtime_error("unknown product '" + product +
                                 "' (try 'products' or 'loadprofile <file>')");
    }
    out_ << "Programming '" << p.name << "' defaults into the fan:\n";
    int written = 0;
    for (const ProfileSetting& s : p.defaults) {
        const RegDef* reg = find_register(s.reg_name);
        if (!reg) {
            record(false, "unknown register '" + s.reg_name + "' in profile");
            continue;
        }
        controller_.write_register(*reg, s.value);
        ++written;
        out_ << "  set " << reg->name << " = " << s.value << " " << reg->unit;
        if (!s.note.empty()) out_ << "   (" << s.note << ")";
        out_ << "\n";
    }
    out_ << "  wrote " << written << " of " << p.defaults.size()
         << " register(s)\n";

    if (p.save_to_flash) {
        out_ << "  saving to flash (app + drive)...\n";
        controller_.save_settings();
        out_ << "  saved.\n";
    }
    record(written == static_cast<int>(p.defaults.size()),
           "programmed product '" + p.name + "'");

    if (p.comm_changes) {
        out_ << "\n  NOTE: communication settings (baud/parity/address) take "
                "effect after a POWER CYCLE.\n";
        out_ << "  After power-cycling, reconnect at: " << p.comm.str() << "\n";
        out_ << "  (or just run 'autoconnect').\n";
    }
}

bool CommandInterpreter::load_profile(const std::string& path) {
    try {
        cmd_load_profile(path);
        return true;
    } catch (const std::exception& e) {
        out_ << "  ERROR: " << e.what() << "\n";
        return false;
    }
}

std::string CommandInterpreter::connection_info() const {
    std::string port = port_name_.empty() ? "(no port)" : port_name_;
    if (!connected_) return port + "  [NOT CONNECTED]";
    const char* par =
        parity_ == Parity::None ? "N" : parity_ == Parity::Even ? "E" : "O";
    return port + "  [CONNECTED " + std::to_string(baud_) + " 8" + par + "1, addr " +
           std::to_string(slave_) + "]";
}

void CommandInterpreter::cmd_disconnect() {
    if (connected_) {
        port_.close();
        connected_ = false;
        out_ << "Disconnected\n";
    }
}

bool CommandInterpreter::record(bool pass, const std::string& message) {
    if (pass) {
        ++passed_;
        out_ << "  PASS: " << message << "\n";
    } else {
        ++failed_;
        out_ << "  FAIL: " << message << "\n";
    }
    return pass;
}

void CommandInterpreter::print_status(const FanStatus& s) {
    out_ << "  State        : " << s.mc_state << " (" << s.mc_state_raw << ")\n";
    out_ << "  Demand       : " << s.demand_value << " % (active source "
         << s.active_source << ")\n";
    out_ << "  Speed        : " << s.measured_speed << " RPM\n";
    out_ << "  Input power  : " << s.input_power << " W\n";
    out_ << "  Bus voltage  : " << s.bus_voltage << " V\n";
    out_ << "  Phase Ia     : " << s.current_a << " mA\n";
    out_ << "  IPM temp     : " << s.ipm_temp << " degC\n";
    if (s.active_faults.empty()) {
        out_ << "  Faults       : none\n";
    } else {
        out_ << "  Faults       : ";
        for (size_t i = 0; i < s.active_faults.size(); ++i) {
            out_ << (i ? ", " : "") << s.active_faults[i];
        }
        out_ << " (faults1=0x" << std::hex << s.faults01 << ", faults2=0x"
             << s.faults02 << std::dec << ")\n";
    }
}

CommandResult CommandInterpreter::execute(const std::string& raw_line) {
    std::string line = strip_comment(raw_line);
    if (line.empty()) return {};

    std::vector<std::string> args = tokenize(line);
    std::string cmd = to_lower(args[0]);
    CommandResult result;

    try {
        if (cmd == "help" || cmd == "?") {
            out_ <<
                "Connection : listports | port <dev> [baud] | baud <n> | parity <n|e|o> |\n"
                "             address <n> | offset <n> | timeout <ms> | retries <n> |\n"
                "             connect | autoconnect | disconnect\n"
                "Products   : products | loadprofile <file> | program <name>\n"
                "Identity   : identify | status | monitor [count] [interval_s]\n"
                "Control    : setspeed <rpm> | setdemand <pct> | start | stop |\n"
                "             direction <std|reverse> | forcemodbus | save\n"
                "Registers  : read <name> [count] | readi <addr> [count] |\n"
                "             readh <addr> [count] | write <name> <value> |\n"
                "             writeraw <addr> <raw>\n"
                "Testing    : expect <name> <min> <max> | wait <sec> | print <text>\n"
                "Other      : list | help | quit\n";
        } else if (cmd == "port") {
            if (args.size() < 2) throw std::runtime_error("usage: port <device> [baud]");
            port_name_ = args[1];
            if (args.size() >= 3) {
                long b; if (!parse_long(args[2], b)) throw std::runtime_error("bad baud");
                baud_ = static_cast<unsigned>(b);
            }
            out_ << "Port set to " << port_name_ << " @ " << baud_ << "\n";
        } else if (cmd == "baud") {
            long b; if (args.size() < 2 || !parse_long(args[1], b))
                throw std::runtime_error("usage: baud <value>");
            baud_ = static_cast<unsigned>(b);
        } else if (cmd == "parity") {
            if (args.size() < 2) throw std::runtime_error("usage: parity <none|even|odd>");
            std::string p = to_lower(args[1]);
            if (p == "none" || p == "n") parity_ = Parity::None;
            else if (p == "even" || p == "e") parity_ = Parity::Even;
            else if (p == "odd" || p == "o") parity_ = Parity::Odd;
            else throw std::runtime_error("parity must be none, even or odd");
            out_ << "Parity set to " << p << "\n";
        } else if (cmd == "address" || cmd == "addr" || cmd == "slave") {
            long a; if (args.size() < 2 || !parse_long(args[1], a) || a < 0 || a > 247)
                throw std::runtime_error("usage: address <0-247>");
            slave_ = static_cast<uint8_t>(a);
            if (connected_) master_.set_slave_address(slave_);
        } else if (cmd == "offset") {
            long o; if (args.size() < 2 || !parse_long(args[1], o))
                throw std::runtime_error("usage: offset <n>");
            address_offset_ = static_cast<int>(o);
            if (connected_) master_.set_address_offset(address_offset_);
        } else if (cmd == "timeout") {
            long t; if (args.size() < 2 || !parse_long(args[1], t) || t <= 0)
                throw std::runtime_error("usage: timeout <ms>");
            response_timeout_ms_ = static_cast<unsigned>(t);
            if (connected_) master_.set_response_timeout_ms(response_timeout_ms_);
        } else if (cmd == "retries") {
            long r; if (args.size() < 2 || !parse_long(args[1], r) || r < 0)
                throw std::runtime_error("usage: retries <n>");
            retries_ = static_cast<unsigned>(r);
            if (connected_) master_.set_retries(retries_);
        } else if (cmd == "connect") {
            cmd_connect();
        } else if (cmd == "autoconnect") {
            cmd_autoconnect();
        } else if (cmd == "disconnect") {
            cmd_disconnect();
        } else if (cmd == "products" || cmd == "profiles") {
            cmd_list_products();
        } else if (cmd == "listports" || cmd == "ports" || cmd == "lsports") {
            cmd_list_serial_ports();
        } else if (cmd == "program") {
            if (args.size() < 2) throw std::runtime_error("usage: program <product>");
            cmd_program(args[1]);
        } else if (cmd == "loadprofile") {
            if (args.size() < 2) throw std::runtime_error("usage: loadprofile <file>");
            cmd_load_profile(args[1]);
        } else if (cmd == "identify") {
            ensure_connected();
            FanIdentity id = controller_.identify();
            out_ << "  Firmware     : " << id.firmware_version << "\n";
            out_ << "  App FW ver   : " << id.app_fw_version << "\n";
            out_ << "  Product var. : " << id.product_variant << "\n";
        } else if (cmd == "status") {
            ensure_connected();
            print_status(controller_.read_status());
        } else if (cmd == "monitor") {
            ensure_connected();
            long count = 10, interval_ms = 1000;
            if (args.size() >= 2) { long v; if (parse_long(args[1], v)) count = v; }
            if (args.size() >= 3) { double v; if (parse_double(args[2], v)) interval_ms = (long)(v*1000); }
            for (long i = 0; i < count; ++i) {
                FanStatus s = controller_.read_status();
                out_ << "[" << (i + 1) << "/" << count << "] state=" << s.mc_state
                     << " speed=" << s.measured_speed << "RPM power=" << s.input_power
                     << "W demand=" << s.demand_value << "% temp=" << s.ipm_temp
                     << "C faults=" << (s.active_faults.empty() ? "none" :
                        std::to_string(s.active_faults.size())) << "\n";
                if (i + 1 < count)
                    std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
            }
        } else if (cmd == "setspeed") {
            ensure_connected();
            long rpm; if (args.size() < 2 || !parse_long(args[1], rpm) || rpm < 0 || rpm > 6000)
                throw std::runtime_error("usage: setspeed <0-6000 rpm>");
            controller_.set_speed(static_cast<uint16_t>(rpm));
            out_ << "  Command speed set to " << rpm << " RPM\n";
        } else if (cmd == "setdemand") {
            ensure_connected();
            double pct; if (args.size() < 2 || !parse_double(args[1], pct))
                throw std::runtime_error("usage: setdemand <0-100 percent>");
            controller_.set_demand(pct);
            out_ << "  Command demand set to " << pct << " %\n";
        } else if (cmd == "start") {
            ensure_connected();
            controller_.start();
            out_ << "  START command sent\n";
        } else if (cmd == "stop") {
            ensure_connected();
            controller_.stop();
            out_ << "  STOP command sent\n";
        } else if (cmd == "direction") {
            ensure_connected();
            if (args.size() < 2) throw std::runtime_error("usage: direction <std|reverse>");
            std::string d = to_lower(args[1]);
            bool std_dir = (d == "std" || d == "ccw" || d == "standard");
            controller_.set_direction(std_dir);
            out_ << "  Direction set to " << (std_dir ? "STD/CCW" : "REVERSE") << "\n";
        } else if (cmd == "forcemodbus") {
            ensure_connected();
            controller_.force_modbus_source();
            out_ << "  Demand source forced to MODBUS\n";
        } else if (cmd == "save" || cmd == "savesettings") {
            ensure_connected();
            controller_.save_settings();
            out_ << "  Settings saved to flash (app + drive)\n";
        } else if (cmd == "read") {
            ensure_connected();
            if (args.size() < 2) throw std::runtime_error("usage: read <name> [count]");
            const RegDef* reg = find_register(args[1]);
            if (!reg) throw std::runtime_error("unknown register '" + args[1] +
                                               "' (try 'list')");
            long count = 1;
            if (args.size() >= 3) parse_long(args[2], count);
            if (count == 1) {
                uint16_t raw = 0;
                double v = controller_.read_register(*reg, &raw);
                out_ << "  " << reg->name << " = " << v << " " << reg->unit
                     << "  (raw 0x" << std::hex << raw << std::dec << " / " << raw << ")\n";
            } else {
                std::vector<uint16_t> regs = (reg->space == RegSpace::Holding)
                    ? master_.read_holding(reg->address, (uint16_t)count)
                    : master_.read_input(reg->address, (uint16_t)count);
                for (size_t i = 0; i < regs.size(); ++i)
                    out_ << "  [" << reg->address + i << "] 0x" << std::hex << regs[i]
                         << std::dec << " / " << regs[i] << "\n";
            }
        } else if (cmd == "readi" || cmd == "readh") {
            ensure_connected();
            long addr, count = 1;
            if (args.size() < 2 || !parse_long(args[1], addr))
                throw std::runtime_error("usage: " + cmd + " <addr> [count]");
            if (args.size() >= 3) parse_long(args[2], count);
            std::vector<uint16_t> regs = (cmd == "readh")
                ? master_.read_holding((uint16_t)addr, (uint16_t)count)
                : master_.read_input((uint16_t)addr, (uint16_t)count);
            for (size_t i = 0; i < regs.size(); ++i)
                out_ << "  [" << addr + (long)i << "] 0x" << std::hex << regs[i]
                     << std::dec << " / " << regs[i] << "\n";
        } else if (cmd == "write") {
            ensure_connected();
            double value;
            if (args.size() < 3 || !parse_double(args[2], value))
                throw std::runtime_error("usage: write <name> <value>");
            const RegDef* reg = find_register(args[1]);
            if (!reg) throw std::runtime_error("unknown register '" + args[1] + "'");
            controller_.write_register(*reg, value);
            out_ << "  wrote " << value << " to " << reg->name << "\n";
        } else if (cmd == "writeraw") {
            ensure_connected();
            long addr, raw;
            if (args.size() < 3 || !parse_long(args[1], addr) || !parse_long(args[2], raw))
                throw std::runtime_error("usage: writeraw <addr> <raw>");
            master_.write_single((uint16_t)addr, (uint16_t)raw);
            out_ << "  wrote 0x" << std::hex << (uint16_t)raw << std::dec
                 << " to holding register " << addr << "\n";
        } else if (cmd == "expect") {
            ensure_connected();
            if (args.size() < 4)
                throw std::runtime_error("usage: expect <name> <min> <max>");
            const RegDef* reg = find_register(args[1]);
            if (!reg) throw std::runtime_error("unknown register '" + args[1] + "'");
            double lo, hi;
            if (!parse_double(args[2], lo) || !parse_double(args[3], hi))
                throw std::runtime_error("expect bounds must be numeric");
            double v = controller_.read_register(*reg);
            std::ostringstream msg;
            msg << reg->name << " = " << v << " " << reg->unit
                << " expected [" << lo << ", " << hi << "]";
            bool pass = (v >= lo && v <= hi);
            record(pass, msg.str());
            result.ok = pass;
            if (!pass && abort_on_failure_) result.quit = true;
        } else if (cmd == "wait" || cmd == "sleep") {
            double sec; if (args.size() < 2 || !parse_double(args[1], sec) || sec < 0)
                throw std::runtime_error("usage: wait <seconds>");
            out_ << "  waiting " << sec << " s...\n";
            std::this_thread::sleep_for(
                std::chrono::milliseconds((long)(sec * 1000)));
        } else if (cmd == "print" || cmd == "echo") {
            std::string rest = line.substr(args[0].size());
            size_t b = rest.find_first_not_of(" \t");
            out_ << (b == std::string::npos ? "" : rest.substr(b)) << "\n";
        } else if (cmd == "pause") {
            if (interactive_) {
                out_ << "  press Enter to continue...";
                std::string dummy;
                std::getline(std::cin, dummy);
            }
        } else if (cmd == "list") {
            for (const RegDef& r : copra_registers()) {
                out_ << "  " << r.name << "  [" << r.address << ", "
                     << (r.space == RegSpace::Holding ? "RW" : "R") << "]  "
                     << r.unit << "  - " << r.desc << "\n";
            }
        } else if (cmd == "quit" || cmd == "exit") {
            result.quit = true;
        } else {
            throw std::runtime_error("unknown command '" + cmd + "' (try 'help')");
        }
    } catch (const std::exception& e) {
        out_ << "  ERROR: " << e.what() << "\n";
        result.ok = false;
        if (!interactive_) {
            ++failed_;
            if (abort_on_failure_) result.quit = true;
        }
    }
    return result;
}

int CommandInterpreter::run_script(std::istream& in) {
    std::string line;
    while (std::getline(in, line)) {
        ++line_number_;
        std::string trimmed = strip_comment(line);
        if (trimmed.empty()) continue;
        out_ << "[" << line_number_ << "] " << trimmed << "\n";
        CommandResult r = execute(line);
        if (r.quit) break;
    }
    cmd_disconnect();
    print_summary();
    return failed_ == 0 ? 0 : 1;
}

void CommandInterpreter::print_summary() const {
    out_ << "\n==== Test summary ====\n";
    out_ << "  Assertions passed: " << passed_ << "\n";
    out_ << "  Assertions failed: " << failed_ << "\n";
    out_ << "  Result: " << (failed_ == 0 ? "OK" : "FAILURES") << "\n";
}

}  // namespace fan
