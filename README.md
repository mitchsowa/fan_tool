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
  -a, --address <n>      Modbus slave address 0-247 (default 247)
  -o, --offset <n>       Register address offset (default 0; see note below)
  -i, --interactive      Force interactive shell even with a script
      --abort-on-fail    Stop the script at the first failed assertion
  -h, --help             Show help
```

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

### Interactive shell

```bash
fan_tool --port /dev/ttyUSB0 -i
fan> connect
fan> identify
fan> setspeed 1500
fan> start
fan> status
fan> stop
fan> quit
```

---

## The `.fan` script language

One command per line. `#` or `//` start a comment. Commands are
case-insensitive. Register names come from `list` (or `docs/REGISTERS.md`).

### Connection
| Command | Meaning |
|---------|---------|
| `port <dev> [baud]` | Set serial device (and optional baud) |
| `baud <n>` / `address <n>` / `offset <n>` | Set link parameters |
| `timeout <ms>` / `retries <n>` | Response timeout / retry count |
| `connect` / `disconnect` | Open / close the port |

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
           fan_controller.h  command_interpreter.h
src/       serial_port_posix.cpp  serial_port_win.cpp
           modbus_rtu.cpp  copra_registers.cpp
           fan_controller.cpp  command_interpreter.cpp  main.cpp
scripts/   commission.fan  smoke.fan
tests/     selftest.cpp  loopback_test.cpp
docs/      REGISTERS.md
```
