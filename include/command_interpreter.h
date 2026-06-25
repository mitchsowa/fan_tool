// command_interpreter.h - Executes fan-tool commands from scripts or the REPL.
//
// One command per line. The same interpreter drives both a .fan test/commission
// script (run non-interactively) and the interactive text shell, so the command
// vocabulary is identical in both. Assertions (`expect`) accumulate pass/fail
// counts to support test reporting.
#ifndef FAN_TOOL_COMMAND_INTERPRETER_H
#define FAN_TOOL_COMMAND_INTERPRETER_H

#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

#include "fan_controller.h"
#include "modbus_rtu.h"
#include "product_profiles.h"
#include "serial_port.h"

namespace fan {

struct CommandResult {
    bool ok = true;          // command executed without error
    bool quit = false;       // user asked to exit the shell
};

class CommandInterpreter {
public:
    // Output goes to `out`; the interactive shell passes std::cout. `interactive`
    // enables REPL-only behaviour (e.g. the `pause` command).
    explicit CommandInterpreter(std::ostream& out, bool interactive = false);

    // Pre-seed connection parameters (from command-line flags) before any
    // script runs. These are overridden by in-script commands.
    void set_default_port(const std::string& port) { port_name_ = port; }
    void set_default_baud(unsigned baud) { baud_ = baud; }
    void set_default_parity(Parity parity) { parity_ = parity; }
    void set_default_address(uint8_t addr) { slave_ = addr; }
    void set_default_offset(int offset) { address_offset_ = offset; }
    void set_abort_on_failure(bool abort) { abort_on_failure_ = abort; }

    // Execute a single command line. Returns the result (ok / quit).
    CommandResult execute(const std::string& line);

    // Load a profile from a file (used by the --profile flag). Returns true on
    // success; prints any error to the output stream.
    bool load_profile(const std::string& path);

    // State accessors (used by the text menu front-end).
    bool connected() const { return connected_; }
    std::string connection_info() const;

    // Run every line of a stream as a script. Returns the process exit code:
    // 0 if all assertions passed and no fatal error occurred, 1 otherwise.
    int run_script(std::istream& in);

    // Assertion tallies.
    int passed() const { return passed_; }
    int failed() const { return failed_; }

    // Print the test summary (counts) to the output stream.
    void print_summary() const;

private:
    // Connection lifecycle.
    void cmd_connect();
    void cmd_disconnect();
    void cmd_autoconnect();
    // Open the port with the given comm settings and probe for a live fan.
    // Returns true and leaves the port connected on success; false otherwise.
    bool try_connect(const CommSettings& comm);
    void ensure_connected();
    void apply_master_config();

    // Program a product profile's defaults into the connected fan.
    void cmd_program(const std::string& product);
    void cmd_list_products();
    void cmd_list_serial_ports();
    void cmd_load_profile(const std::string& path);
    // Read every writable configuration register from the connected fan and
    // write a runnable .fan clone script (writeraw lines + save) to `path`.
    void cmd_dump_settings(const std::string& path);
    // Resolve a profile by name: loaded-from-file profiles first, then a
    // matching <name>.profile / profiles/<name>.profile file, then the
    // compiled-in defaults. Returns false if none match.
    bool resolve_profile(const std::string& name, ProductProfile& out);

    // Helpers.
    void print_status(const FanStatus& s);
    bool record(bool pass, const std::string& message);  // tally + print

    std::ostream& out_;
    bool interactive_;
    bool abort_on_failure_ = false;

    SerialPort port_;
    ModbusMaster master_;
    FanController controller_;
    bool connected_ = false;

    // Connection parameters.
    std::string port_name_;
    unsigned baud_ = 115200;
    Parity parity_ = Parity::None;
    uint8_t slave_ = 247;
    int address_offset_ = 0;
    unsigned response_timeout_ms_ = 600;
    unsigned retries_ = 2;

    // Profiles loaded from text files this session (take precedence over the
    // compiled-in defaults).
    std::vector<ProductProfile> loaded_profiles_;

    int passed_ = 0;
    int failed_ = 0;
    int line_number_ = 0;
};

}  // namespace fan

#endif  // FAN_TOOL_COMMAND_INTERPRETER_H
