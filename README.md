# COPRA EC Fan Test & Commissioning Tool

A small, dependency-free **C++17** command-line tool for testing and
commissioning Nicotra Gebhardt / Regal Rexnord **COPRA** EC fans over their
**Modbus RTU / RS485** interface.

It drives the fan from a plain-text **test/commission script** (a `.fan` file)
so an acceptance sequence can be run repeatably and reported as pass/fail. The
same command set is also available in an interactive text shell. The tool is
**text-only** and builds on **Linux first** and **Windows** (command prompt)
from the same source.

> Protocol and register details are taken from the *COPRA Products Modbus
> Specification – User Registers, Release 1.0* and the *COPRA Configuration Tool
> Manual*. See [`docs/REGISTERS.md`](docs/REGISTERS.md) for the register map the
> tool uses.

---

## How it talks to the fan

The COPRA presents a Modbus RTU follower (slave) on an RS485 link:

| Parameter | Value (factory default) |
|-----------|-------------------------|
| Physical  | RS485, 3-wire (A, B, GND), half-duplex |
| Baud      | 115200 (use 9600 on busy multi-drop buses) |
| Framing   | 8 data bits, 1 stop bit, no parity (8N1) |
| Address   | 247 (`0xF7`); range 1–247, 0 = broadcast |
| Function codes | 0x03 read holding, 0x04 read input, 0x06 write single, 0x10 write multiple |
| CRC       | CRC-16, polynomial `0xA001`, init `0xFFFF`, low byte first |

On a PC you reach the bus through a **USB-to-RS485 adapter**, which appears as
`/dev/ttyUSB0` (Linux) or `COM3` (Windows).

---

## Building

Requires CMake ≥ 3.10 and a C++17 compiler.

### Linux

```bash
cmake -S . -B build
cmake --build build -j
ctest --test-dir build        # hardware-free unit + loopback tests
```

Binary: `build/fan_tool`. Add your user to the `dialout` group (or use `sudo`)
to access the serial device.

### Windows (command prompt)

```bat
cmake -S . -B build -G "Visual Studio 17 2022"
cmake --build build --config Release
```

Binary: `build\Release\fan_tool.exe`. MinGW (`-G "MinGW Makefiles"`) works too.

The serial layer has two backends selected at compile time — POSIX `termios`
and Win32 `COM` — so no source changes are needed between platforms.

---

## Usage

```
fan_tool [options] [script.fan]

  -p, --port <device>    Serial port (/dev/ttyUSB0 or COM3)
  -b, --baud <rate>      Baud rate (default 115200)
      --parity <n|e|o>   Parity: none/even/odd (default none)
  -a, --address <n>      Modbus slave address 0-247 (default 247)
  -o, --offset <n>       Register address offset (default 0; see note below)
      --autoconnect      Probe known comm settings until the fan responds
      --program <name>   Auto-connect, program a product profile, then exit
      --list-products    List available product profiles and exit
  -i, --interactive      Force interactive shell even with a script
      --abort-on-fail    Stop the script at the first failed assertion
  -h, --help             Show help
```

### Program a product's defaults

The tool ships with **product profiles** — named bundles of default register
values to program into a fan so it is configured consistently for a given
product. The first is the **e360**.

```bash
fan_tool --list-products                          # show profiles
fan_tool --port /dev/ttyUSB0 --program e360       # auto-connect + program + save
```

`--program` runs `autoconnect` first, so it works whether the fan is fresh from
the factory (115200 8N1, addr 247) or already on the **e360** bus settings
(**19200 8E1, address 11**). It writes the profile's registers, saves them to
flash, and reminds you that comm-setting changes require a **power cycle**.

In a script or the shell the same is available as `products` and `program
<name>` (see [`scripts/program_e360.fan`](scripts/program_e360.fan)).

#### Profiles are editable text files

A profile is a plain text file (see [`profiles/e360.profile`](profiles/e360.profile)).
`program e360` looks for `profiles/e360.profile` (then `e360.profile`) before
falling back to the compiled-in defaults, so you tune a product **without
rebuilding** — just edit the file. Load one explicitly with `--profile <file>`,
the `loadprofile <file>` command, or menu item 15.

```
# profiles/e360.profile
name        = e360
description = e360 plenum fan
comm.baud    = 19200
comm.parity  = even          # none | even | odd
comm.address = 11
save_to_flash = true

set modbus_baud      = 19200  # set <register> = <value>   [# note]
set modbus_parity    = 2      # 0=none 1=odd 2=even
set modbus_address   = 11
set direction        = 9      # 9 = STD/CCW
set modbus_priority  = 1
set hb_timeout       = 20
```
Register names are those from `list` / `docs/REGISTERS.md`. To add a new
product, copy `e360.profile` to `profiles/<name>.profile` and edit it — no code
changes needed.

### Clone a fan's settings onto another fan

To copy one fan's configuration to others, dump its settings to a `.fan` script
and replay it:

```bash
fan_tool --port /dev/ttyUSB0 --shell
fan> connect
fan> dumpsettings my_fan.fan        # or menu item 20

# then on each target fan:
fan_tool --port /dev/ttyUSB0 my_fan.fan
```

`dumpsettings` reads every writable configuration register the tool knows
about (comm settings, demand multiplexer, digital-input functions, heartbeat,
direction, …) and writes a runnable script of `writeraw` lines ending in
`save`. It uses **raw** register values so they reload exactly. Command/runtime
registers (start, command speed/demand, flash commands) are excluded. Because
comm-setting changes only apply after a power cycle, **power-cycle the target
fan** after running the clone.

> The dump covers the registers in the tool's map (see `list`), not literally
> every address in the device — extend the table in
> [`src/copra_registers.cpp`](src/copra_registers.cpp) to capture more.

### Digital inputs and the motor-enable interlock

Each digital input (DIN1–3) has a configurable function set by `din1_function` /
`din2_function` / `din3_function` (registers 33395–33397):

| Value | Function | | Value | Function |
|------:|----------|-|------:|----------|
| 0 | input disabled | | 4 | firemode enable |
| 1 | **motor enable** | | 5 | alarm reset |
| 2 | motor start | | 6–9 | discrete demand 0–3 |
| 3 | set direction | | | |

`din_enable` (33385) enables each input (bit0=DIN1…), `din_polarity` (33386)
sets normally-open(0)/normally-closed(1), and `din_status` (33322) reads the
live input state.

> ⚠️ **A digital input set to *motor enable* takes priority over the Modbus
> Start Command.** If such an input is open/inactive, the drive stays in `IDLE`
> and ignores `start` — even with the Modbus demand source selected. To run a
> fan over Modbus alone, set that input's function to `0` (disabled), `save`,
> and **power-cycle** (the digital-input function is only re-read at boot).

### Auto-connect / comm fallback

If a fan does not answer at the default settings, `autoconnect` (or
`--autoconnect`) probes each known comm setting in turn — factory default first,
then every product profile's settings — and adopts the first that responds:

```bash
fan_tool --port /dev/ttyUSB0 --autoconnect -i
```

> **e360 note:** these units run at **19200 baud, EVEN parity, Modbus address
> 11**. If the default 115200/8N1/247 doesn't connect, that's the fallback
> `autoconnect` will find (or set it manually with `--baud 19200 --parity even
> --address 11`).

To add another product, append a `ProductProfile` to
[`src/product_profiles.cpp`](src/product_profiles.cpp) — the e360 entry is the
template.

### Run a commissioning script

```bash
# Linux
fan_tool --port /dev/ttyUSB0 scripts/commission.fan

# Windows
fan_tool.exe --port COM3 scripts\commission.fan
```

The tool prints each step and a final summary. **Exit code is 0 if every
assertion passed, 1 otherwise** — so it drops straight into a CI pipeline or a
production test jig.

### Text menu (default interactive mode)

Running with a port but no script drops you into a numbered text menu:

```bash
fan_tool --port /dev/ttyUSB0
```
```
========================================================
  COPRA EC Fan - Test & Commissioning Tool
  /dev/ttyUSB0  [NOT CONNECTED]
========================================================
  Connection                Control
    1) Configure port/comm    8) Set speed (RPM)
    2) Auto-connect           9) Set demand (%)
    3) Connect               10) Start motor
    4) Disconnect            11) Stop motor
                             12) Set direction
  Information                13) Force Modbus source
    5) Identify fan
    6) Show status          Products / commissioning
    7) Monitor (live)        14) List product profiles
                             15) Load profile from file
  Other                      16) Program a profile
   18) Run test script (.fan) 17) Save settings to flash
   19) Command prompt (advanced) 20) Dump settings to file
    0) Quit
  Select:
```
Each item prompts for any values it needs. Menu item **19** drops to the raw
command prompt; every menu action just runs the matching command, so behaviour
is identical to scripts.

### Command shell

For power users, `--shell` gives the raw one-command-per-line prompt:

```bash
fan_tool --port /dev/ttyUSB0 --shell
fan> autoconnect
fan> identify
fan> setspeed 1500
fan> start
fan> quit
```

---

## The `.fan` script language

One command per line. `#` or `//` start a comment. Commands are
case-insensitive. Register names come from `list` (or `docs/REGISTERS.md`).

### Connection
| Command | Meaning |
|---------|---------|
| `listports` | List the serial ports present on the system |
| `port <dev> [baud]` | Set serial device (and optional baud) |
| `baud <n>` / `parity <n\|e\|o>` / `address <n>` / `offset <n>` | Set link parameters |
| `timeout <ms>` / `retries <n>` | Response timeout / retry count |
| `connect` / `disconnect` | Open / close the port |
| `autoconnect` | Probe known comm settings until the fan responds |

### Products
| Command | Meaning |
|---------|---------|
| `products` | List available product profiles |
| `program <name>` | Program a product's default registers (e.g. `program e360`) |
| `dumpsettings <file>` | Read this fan's config registers into a runnable `.fan` clone script |

### Identity & monitoring
| Command | Meaning |
|---------|---------|
| `identify` | Read firmware revision, app version, product variant |
| `status` | Read state, speed, power, bus voltage, current, temp, faults |
| `monitor [count] [interval_s]` | Repeatedly print a one-line status |

### Control / commissioning
| Command | Meaning |
|---------|---------|
| `setspeed <rpm>` | Command speed in RPM (0–6000) |
| `setdemand <pct>` | Command demand in % (overrides speed) |
| `start` / `stop` | Start / stop the motor |
| `direction <std\|reverse>` | Set rotation (COPRA supports STD/CCW) |
| `forcemodbus` | Force Modbus to be the active demand source |
| `save` | Persist holding-register settings to flash (app + drive) |

### Registers
| Command | Meaning |
|---------|---------|
| `read <name> [count]` | Read a named register (engineering value) |
| `readi <addr> [count]` / `readh <addr> [count]` | Raw input / holding read |
| `write <name> <value>` | Write engineering value to a holding register |
| `writeraw <addr> <raw>` | Write a raw 16-bit value to a holding register |

### Testing & flow
| Command | Meaning |
|---------|---------|
| `expect <name> <min> <max>` | Assert a register reading is within range (PASS/FAIL) |
| `wait <seconds>` | Sleep (fractional seconds allowed) |
| `print <text>` | Echo a line of text |
| `pause` | Wait for Enter (interactive only) |
| `list` / `help` / `quit` | List registers / help / exit |

See [`scripts/commission.fan`](scripts/commission.fan) for a full annotated
acceptance sequence and [`scripts/smoke.fan`](scripts/smoke.fan) for a minimal
connectivity check.

---

## How commissioning works

1. **Identify** the unit and read **baseline status** (should be `IDLE`, no
   faults, DC bus present).
2. Set **direction** (`STD`) and make **Modbus** the demand source.
3. Write a **command speed** (or demand %) and send **start**. The COPRA's
   `Command Demand` register takes priority over `Command Speed`, so
   `setspeed` clears demand first.
4. `expect` the measured speed / state / faults to confirm the fan responds.
5. **Stop**, verify spin-down, and optionally **save** configuration to flash.

> Command speed, demand, start and direction are **runtime** registers — the
> drive intentionally does *not* persist them across a power cycle, so a host
> must re-issue them on startup. `save` persists the configuration registers
> (direction, priorities, Modbus settings).

---

## A note on register addressing

The COPRA spec lists each register by its **Modbus address**. The tool sends
these addresses on the wire verbatim (offset `0`). Some Modbus hosts use the
"register number minus one" convention; if your unit replies with *Illegal Data
Address* for addresses you know exist, try `--offset -1` (or the `offset -1`
command) to shift every address down by one.

---

## Testing without a fan

Two hardware-free test suites run under `ctest`:

- **`fan_selftest`** — CRC-16 reference vectors and register value
  decode/encode.
- **`fan_loopback`** — a mock COPRA slave on a pseudo-terminal exercises the
  full request → wire → response path (read input/holding, signed decode,
  write single/multiple). POSIX only.

---

## Project layout

```
include/   serial_port.h  modbus_rtu.h  copra_registers.h
           product_profiles.h  fan_controller.h
           command_interpreter.h  menu.h
src/       serial_port_posix.cpp  serial_port_win.cpp
           modbus_rtu.cpp  copra_registers.cpp  product_profiles.cpp
           fan_controller.cpp  command_interpreter.cpp  menu.cpp  main.cpp
profiles/  e360.profile                  (editable product profiles)
scripts/   commission.fan  smoke.fan  program_e360.fan
tests/     selftest.cpp  loopback_test.cpp
docs/      REGISTERS.md
```
