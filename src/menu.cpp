// menu.cpp - Implementation of the interactive text menu.
#include "menu.h"

#include <fstream>
#include <iostream>
#include <string>

namespace fan {

namespace {

// Print a prompt and read one line of input. Returns false on EOF.
bool prompt_line(std::istream& in, std::ostream& out, const std::string& label,
                 std::string& value) {
    out << label << std::flush;
    if (!std::getline(in, value)) return false;
    // Trim surrounding whitespace.
    size_t b = value.find_first_not_of(" \t\r\n");
    size_t e = value.find_last_not_of(" \t\r\n");
    value = (b == std::string::npos) ? "" : value.substr(b, e - b + 1);
    return true;
}

void show_menu(CommandInterpreter& interp, std::ostream& out) {
    out << "\n";
    out << "========================================================\n";
    out << "  COPRA EC Fan - Test & Commissioning Tool\n";
    out << "  " << interp.connection_info() << "\n";
    out << "========================================================\n";
    out << "  Connection                Control\n";
    out << "    1) Configure port/comm    8) Set speed (RPM)\n";
    out << "    2) Auto-connect           9) Set demand (%)\n";
    out << "    3) Connect               10) Start motor\n";
    out << "    4) Disconnect            11) Stop motor\n";
    out << "                             12) Set direction\n";
    out << "  Information                13) Force Modbus source\n";
    out << "    5) Identify fan        \n";
    out << "    6) Show status          Products / commissioning\n";
    out << "    7) Monitor (live)        14) List product profiles\n";
    out << "                             15) Load profile from file\n";
    out << "  Other                      16) Program a profile\n";
    out << "   18) Run test script (.fan) 17) Save settings to flash\n";
    out << "   19) Command prompt (advanced) 20) Dump settings to file\n";
    out << "    0) Quit\n";
    out << "--------------------------------------------------------\n";
}

// Read one or two prompted arguments and run "<cmd> <args>".
void do_with_prompt(CommandInterpreter& interp, std::istream& in,
                    std::ostream& out, const std::string& cmd,
                    const std::string& label) {
    std::string v;
    if (!prompt_line(in, out, label, v)) return;
    if (v.empty()) { out << "  (cancelled)\n"; return; }
    interp.execute(cmd + " " + v);
}

void run_advanced_prompt(CommandInterpreter& interp, std::istream& in,
                         std::ostream& out) {
    out << "  Advanced command mode. Type 'help' for commands, 'back' to "
           "return to the menu.\n";
    std::string line;
    while (true) {
        out << "cmd> " << std::flush;
        if (!std::getline(in, line)) return;
        std::string t = line;
        size_t b = t.find_first_not_of(" \t\r\n");
        if (b != std::string::npos) t = t.substr(b);
        if (t == "back" || t == "menu") return;
        if (t.empty()) continue;
        CommandResult r = interp.execute(line);
        if (r.quit) return;
    }
}

void run_script_file(CommandInterpreter& interp, std::istream& in,
                     std::ostream& out) {
    std::string path;
    if (!prompt_line(in, out, "  Script file (.fan): ", path)) return;
    if (path.empty()) { out << "  (cancelled)\n"; return; }
    std::ifstream file(path);
    if (!file) { out << "  ERROR: cannot open '" << path << "'\n"; return; }
    std::string line;
    while (std::getline(file, line)) {
        out << "  | " << line << "\n";
        if (interp.execute(line).quit) break;
    }
    out << "  (script finished: " << interp.passed() << " passed, "
        << interp.failed() << " failed)\n";
}

void configure_comm(CommandInterpreter& interp, std::istream& in,
                    std::ostream& out) {
    std::string v;
    interp.execute("listports");  // show what's currently plugged in
    out << "  (press Enter to keep the current value)\n";
    if (prompt_line(in, out, "  Serial port  : ", v) && !v.empty())
        interp.execute("port " + v);
    if (prompt_line(in, out, "  Baud rate    : ", v) && !v.empty())
        interp.execute("baud " + v);
    if (prompt_line(in, out, "  Parity (n/e/o): ", v) && !v.empty())
        interp.execute("parity " + v);
    if (prompt_line(in, out, "  Address (0-247): ", v) && !v.empty())
        interp.execute("address " + v);
}

}  // namespace

void run_menu(CommandInterpreter& interp, std::istream& in, std::ostream& out) {
    while (true) {
        show_menu(interp, out);
        std::string choice;
        if (!prompt_line(in, out, "  Select: ", choice)) break;  // EOF
        if (choice.empty()) continue;

        if (choice == "0" || choice == "q" || choice == "quit") {
            break;
        } else if (choice == "1") {
            configure_comm(interp, in, out);
        } else if (choice == "2") {
            interp.execute("autoconnect");
        } else if (choice == "3") {
            interp.execute("connect");
        } else if (choice == "4") {
            interp.execute("disconnect");
        } else if (choice == "5") {
            interp.execute("identify");
        } else if (choice == "6") {
            interp.execute("status");
        } else if (choice == "7") {
            do_with_prompt(interp, in, out, "monitor",
                           "  Samples [count] [interval_s], e.g. '20 1': ");
        } else if (choice == "8") {
            do_with_prompt(interp, in, out, "setspeed", "  Speed (RPM): ");
        } else if (choice == "9") {
            do_with_prompt(interp, in, out, "setdemand", "  Demand (%): ");
        } else if (choice == "10") {
            interp.execute("start");
        } else if (choice == "11") {
            interp.execute("stop");
        } else if (choice == "12") {
            do_with_prompt(interp, in, out, "direction",
                           "  Direction (std/reverse): ");
        } else if (choice == "13") {
            interp.execute("forcemodbus");
        } else if (choice == "14") {
            interp.execute("products");
        } else if (choice == "15") {
            do_with_prompt(interp, in, out, "loadprofile",
                           "  Profile file path: ");
        } else if (choice == "16") {
            do_with_prompt(interp, in, out, "program",
                           "  Product/profile name (e.g. e360): ");
        } else if (choice == "17") {
            interp.execute("save");
        } else if (choice == "18") {
            run_script_file(interp, in, out);
        } else if (choice == "19") {
            run_advanced_prompt(interp, in, out);
        } else if (choice == "20") {
            do_with_prompt(interp, in, out, "dumpsettings",
                           "  Output file (.fan): ");
        } else {
            out << "  Unrecognised choice '" << choice << "'\n";
        }
    }
    interp.execute("disconnect");
    out << "Bye.\n";
}

}  // namespace fan
