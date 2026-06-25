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
    cfg.baud = baud_;  // COPRA is 8N1; data/stop/parity left at defaults
    port_.open(port_name_, cfg);
    apply_master_config();
    connected_ = true;
    out_ << "Connected to " << port_name_ << " @ " << baud_
         << " 8N1, slave address " << static_cast<int>(slave_) << "\n";
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
                "Connection : port <dev> [baud] | baud <n> | address <n> | "
                "offset <n> | timeout <ms> | retries <n> | connect | disconnect\n"
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
        } else if (cmd == "disconnect") {
            cmd_disconnect();
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
