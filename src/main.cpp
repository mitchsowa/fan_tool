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
#include <vector>

#include "command_interpreter.h"
#include "menu.h"
#include "product_profiles.h"

namespace {

void print_usage(const char* prog) {
    std::cout <<
        "COPRA EC fan test & commissioning tool\n\n"
        "Usage: " << prog << " [options] [script.fan]\n\n"
        "Options:\n"
        "  -p, --port <device>    Serial port (e.g. /dev/ttyUSB0 or COM3)\n"
        "  -b, --baud <rate>      Baud rate (default 115200)\n"
        "      --parity <n|e|o>   Parity: none/even/odd (default none)\n"
        "  -a, --address <n>      Modbus slave address 0-247 (default 247)\n"
        "  -o, --offset <n>       Register address offset (default 0)\n"
        "      --autoconnect      Probe known comm settings (default, then\n"
        "                         each product) until the fan responds\n"
        "      --program <name>   Auto-connect, program a product profile's\n"
        "                         defaults to the fan, then exit (e.g. e360)\n"
        "      --profile <file>   Load a product profile from a text file\n"
        "                         (repeatable)\n"
        "      --list-products    List available product profiles and exit\n"
        "  -i, --menu             Text menu interface (default when no script)\n"
        "      --shell            Raw command shell instead of the menu\n"
        "      --abort-on-fail    Stop the script at the first failed assertion\n"
        "  -h, --help             Show this help\n\n"
        "Examples:\n"
        "  " << prog << " -p /dev/ttyUSB0                 (text menu)\n"
        "  " << prog << " -p /dev/ttyUSB0 commission.fan\n"
        "  " << prog << " -p /dev/ttyUSB0 --program e360\n"
        "  " << prog << " -p /dev/ttyUSB0 --profile profiles/e360.profile -i\n"
        "  " << prog << " --list-products\n";
}

}  // namespace

int main(int argc, char** argv) {
    std::string port;
    unsigned baud = 115200;
    fan::Parity parity = fan::Parity::None;
    int address = 247;
    int offset = 0;
    bool interactive = false;
    bool shell = false;
    bool abort_on_fail = false;
    bool autoconnect = false;
    std::string program_product;
    std::string script_path;
    std::vector<std::string> profile_files;

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
        } else if (arg == "--parity") {
            std::string p = next("--parity");
            if (p == "n" || p == "none") parity = fan::Parity::None;
            else if (p == "e" || p == "even") parity = fan::Parity::Even;
            else if (p == "o" || p == "odd") parity = fan::Parity::Odd;
            else { std::cerr << "error: bad parity '" << p << "'\n"; return 2; }
        } else if (arg == "-a" || arg == "--address") {
            address = std::stoi(next("--address"));
        } else if (arg == "-o" || arg == "--offset") {
            offset = std::stoi(next("--offset"));
        } else if (arg == "--autoconnect") {
            autoconnect = true;
        } else if (arg == "--program") {
            program_product = next("--program");
        } else if (arg == "--profile") {
            profile_files.push_back(next("--profile"));
        } else if (arg == "--list-products") {
            fan::CommandInterpreter tmp(std::cout, false);
            tmp.execute("products");
            return 0;
        } else if (arg == "-i" || arg == "--interactive" || arg == "--menu") {
            interactive = true;
        } else if (arg == "--shell") {
            interactive = true;
            shell = true;
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
    interp.set_default_parity(parity);
    interp.set_default_address(static_cast<uint8_t>(address));
    interp.set_default_offset(offset);
    interp.set_abort_on_failure(abort_on_fail);

    // Load any profile files supplied on the command line.
    for (const std::string& pf : profile_files) interp.load_profile(pf);

    // --program <product>: auto-connect, program the profile, then exit.
    if (!program_product.empty()) {
        if (port.empty()) {
            std::cerr << "error: --program requires --port\n";
            return 2;
        }
        fan::CommandResult c = interp.execute("autoconnect");
        bool ok = c.ok;
        if (ok) ok = interp.execute("program " + program_product).ok;
        interp.execute("disconnect");
        interp.print_summary();
        return (ok && interp.failed() == 0) ? 0 : 1;
    }

    if (run_as_script) {
        std::ifstream file(script_path);
        if (!file) {
            std::cerr << "error: cannot open script '" << script_path << "'\n";
            return 2;
        }
        std::cout << "Running script: " << script_path << "\n\n";
        return interp.run_script(file);
    }

    if (autoconnect) interp.execute("autoconnect");

    if (!shell) {
        // Default interactive experience: the text menu.
        fan::run_menu(interp, std::cin, std::cout);
        return 0;
    }

    // Raw command shell (--shell).
    std::cout << "COPRA fan tool - command shell. Type 'help' for commands, "
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
