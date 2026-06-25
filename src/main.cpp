// main.cpp - Command-line entry point for the COPRA fan test/commission tool.
//
// Usage:
//   fan_tool [options] [script.fan]
//
// With a script file, the tool runs it non-interactively and exits with status
// 0 if every assertion passed, 1 otherwise. With no script (or --interactive),
// it opens a text shell. Connection flags pre-seed the interpreter; they can be
// overridden by `port`/`baud`/`address` commands inside a script.
#include <fstream>
#include <iostream>
#include <string>

#include "command_interpreter.h"

namespace {

void print_usage(const char* prog) {
    std::cout <<
        "COPRA EC fan test & commissioning tool\n\n"
        "Usage: " << prog << " [options] [script.fan]\n\n"
        "Options:\n"
        "  -p, --port <device>    Serial port (e.g. /dev/ttyUSB0 or COM3)\n"
        "  -b, --baud <rate>      Baud rate (default 115200)\n"
        "  -a, --address <n>      Modbus slave address 0-247 (default 247)\n"
        "  -o, --offset <n>       Register address offset (default 0)\n"
        "  -i, --interactive      Force interactive shell even with a script\n"
        "      --abort-on-fail    Stop the script at the first failed assertion\n"
        "  -h, --help             Show this help\n\n"
        "Examples:\n"
        "  " << prog << " -p /dev/ttyUSB0 commission.fan\n"
        "  " << prog << " -p COM3 -a 247 -i\n"
        "  " << prog << " --port /dev/ttyUSB0          (interactive shell)\n";
}

}  // namespace

int main(int argc, char** argv) {
    std::string port;
    unsigned baud = 115200;
    int address = 247;
    int offset = 0;
    bool interactive = false;
    bool abort_on_fail = false;
    std::string script_path;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto next = [&](const char* name) -> std::string {
            if (i + 1 >= argc) {
                std::cerr << "error: " << name << " requires an argument\n";
                std::exit(2);
            }
            return argv[++i];
        };
        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "-p" || arg == "--port") {
            port = next("--port");
        } else if (arg == "-b" || arg == "--baud") {
            baud = static_cast<unsigned>(std::stoul(next("--baud")));
        } else if (arg == "-a" || arg == "--address") {
            address = std::stoi(next("--address"));
        } else if (arg == "-o" || arg == "--offset") {
            offset = std::stoi(next("--offset"));
        } else if (arg == "-i" || arg == "--interactive") {
            interactive = true;
        } else if (arg == "--abort-on-fail") {
            abort_on_fail = true;
        } else if (!arg.empty() && arg[0] == '-') {
            std::cerr << "error: unknown option '" << arg << "'\n";
            print_usage(argv[0]);
            return 2;
        } else {
            script_path = arg;
        }
    }

    if (address < 0 || address > 247) {
        std::cerr << "error: address must be 0-247\n";
        return 2;
    }

    bool run_as_script = !script_path.empty() && !interactive;

    fan::CommandInterpreter interp(std::cout, /*interactive=*/!run_as_script);
    if (!port.empty()) interp.set_default_port(port);
    interp.set_default_baud(baud);
    interp.set_default_address(static_cast<uint8_t>(address));
    interp.set_default_offset(offset);
    interp.set_abort_on_failure(abort_on_fail);

    if (run_as_script) {
        std::ifstream file(script_path);
        if (!file) {
            std::cerr << "error: cannot open script '" << script_path << "'\n";
            return 2;
        }
        std::cout << "Running script: " << script_path << "\n\n";
        return interp.run_script(file);
    }

    // Interactive shell.
    std::cout << "COPRA fan tool - interactive shell. Type 'help' for commands, "
                 "'quit' to exit.\n";
    if (!port.empty()) {
        std::cout << "(port " << port << " @ " << baud << ", address " << address
                  << " - type 'connect' to open)\n";
    }
    std::string line;
    while (true) {
        std::cout << "fan> " << std::flush;
        if (!std::getline(std::cin, line)) break;  // EOF
        fan::CommandResult r = interp.execute(line);
        if (r.quit) break;
    }
    std::cout << "Bye.\n";
    return 0;
}
