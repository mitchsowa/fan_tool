# Modbus-RTU bus sniffer (passive)

Throwaway diagnostic tools used to troubleshoot a third-party master (e.g. a
PLC) talking to the fan on a shared RS485 pair. They open the serial port
**read-only** and never transmit, so they are safe to run on a live bus — wire
the adapter's A/B in parallel with the existing master and slave.

> These were written to diagnose a PLC integration; see
> [`docs/PLC_BUS_DIAGNOSIS.md`](../../docs/PLC_BUS_DIAGNOSIS.md) for the
> write-up that came out of them. POSIX only (termios).

## Tools

| File | What it does |
|------|--------------|
| `capture.cpp` | Opens the port at a given baud/parity (8 data, 1 stop) and dumps every received byte verbatim to stdout. |
| `parse.cpp`   | Offline RTU stream parser with **CRC resync** — reads a raw dump, recovers true frame boundaries by accepting only CRC-valid frames, and pairs each response with its request. |
| `sniff.cpp`   | Real-time sniffer: frames by the 3.5-char inter-frame gap and decodes on the fly. Handy for a quick look; the capture→parse pair is more robust when frames arrive back-to-back. |
| `watch.sh`    | Rolling live monitor: captures short windows and prints one status line per window (writes seen, `mc_state`, speed, power, current, bus V, faults). Uses `capture` + `parse` from the same directory. |

## Build

```sh
g++ -O2 -o capture capture.cpp
g++ -O2 -o parse   parse.cpp
g++ -O2 -o sniff   sniff.cpp
```

## Use

```sh
# 1. Capture raw traffic, then decode offline (most reliable)
./capture /dev/ttyUSB0 19200 E > raw.bin      # Ctrl-C to stop
./parse   raw.bin

# 2. Quick real-time view
./sniff /dev/ttyUSB0 19200 E 11               # device baud parity watch_addr

# 3. Rolling live status (3-second windows)
./watch.sh 3
```

Arguments default to `/dev/ttyUSB0 19200 E` (and watch address `11` for
`sniff`). Parity is `N`, `E`, or `O`.

## Tip — FTDI latency timer

A USB-serial adapter batches received bytes on its latency timer (default
16 ms), which defeats the inter-frame-gap framing and makes adjacent frames
look glued together (false CRC errors). Drop it before sniffing:

```sh
echo 1 | sudo tee /sys/bus/usb-serial/devices/ttyUSB0/latency_timer
```

The CRC-resync parser (`parse`) recovers boundaries either way, but a low
latency timer also makes the real-time `sniff` framing reliable.
