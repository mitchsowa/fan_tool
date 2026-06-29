# PLC ↔ Fan Bus Diagnosis (address 11)

Captured 2026-06-29 by passively sniffing the RS485 pair while a PLC (Modbus
master) drove the COPRA fan (slave, address 11). The sniffer opened the port
**read-only** and never transmitted onto the half-duplex bus.

- **Link:** 19200 baud, 8E1, slave address **11**
- **Capture:** 10 s, 7370 raw bytes, **800 frames decoded, 0 exceptions,
  2 bytes skipped** (the partial frame present at capture start)

## Verdict

The comms layer is **flawless** — correct baud/parity/address, valid CRCs, and
the fan ACKs every request. This is **not** a wiring, baud, parity, or address
problem.

The fault is in the PLC's **register addressing: every command write lands one
register too low (a classic off-by-one / 0-based-vs-1-based offset).**

## Evidence — what the PLC sends each cycle

| PLC writes to | value | Fan register actually there | What the PLC intended (addr + 1) |
|---|---|---|---|
| 35944 | 2400 | *(unused)* | **35945 `cmd_speed` = 2400 RPM** |
| 35945 | 40 | `cmd_speed` | **35946 `cmd_demand` = 40** |
| 35946 | 1 | `cmd_demand` | **35947 `start` = 1 (START)** |
| 35947 | **9** | `start` | **35948 `direction` = 9 (STD)** |

The shift is exactly **−1 on all four registers**, and two values prove it:

- **`9`** is the code for **STD direction** (reg 35948) — the PLC put it in
  `start` (35947).
- **`1`** is the **START** command (reg 35947) — the PLC put it in
  `cmd_demand` (35946).

So the real `start` register (35947) only ever receives `9` (never the start
command), and the real demand register (35946) receives `1` = 0.01% ≈ zero.
That is why the fan never spins.

### Metering read confirms IDLE

```
REQ  addr=11  ReadInput  read reg=41 qty=11
RSP  addr=11  ReadInput  [41]=0 [42]=0 [43]=0 [44]=2 [45]=9 [46]=696 [47]=0 [48]=0 [49]=0 [50]=25 [51]=0
```

- `[41]` `mc_state` = **0 (IDLE)** — fan is not started
- `[46]` `bus_voltage` = 696, `[50]` `ipm_temp` = 25 °C — fan is powered and
  healthy, just never commanded to run

## The fix

In the PLC, **add 1 to every command register address** (the four control
writes). The PLC is transmitting `address − 1`. Two ways to correct it:

- Set the PLC's command tags to `cmd_speed=35946`, `cmd_demand=35947`,
  `start=35948`, `direction=35949` (each = fan address + 1), **or**
- If the PLC/driver has a "PDU / 0-based addressing" or "subtract-one offset"
  toggle, flip it so it sends the literal address.

After the bump, the start command (`1`) reaches 35947 and demand reaches 35946,
and the fan will run.

### Secondary notes (after the offset is fixed)

- **Demand scaling:** `cmd_demand` is ×0.01 %, so `40` = 0.40 %. For 40 % the
  PLC must write `4000`. (Or drive by `cmd_speed` instead.)
- **DIN motor-enable interlock:** if this fan still has `din1_function` = 1
  (motor enable) and that input is open, it stays IDLE even with a correct
  Modbus start. See [REGISTERS.md](REGISTERS.md) → Digital inputs.

## How this was captured

Two throwaway tools were used (passive, read-only):

1. `capture` — opens the port at 19200 8E1 and dumps every received byte
   verbatim to a file.
2. `parse` — offline Modbus-RTU stream parser with **CRC resync**: walks the
   byte stream and accepts only CRC-valid frames of recognized shapes, pairing
   each slave response with the preceding request.

> **Tip — FTDI latency timer.** A USB-serial adapter batches received bytes on
> its latency timer (default 16 ms), which defeats the 3.5-character
> inter-frame gap used for RTU framing and makes adjacent frames look glued
> together (false CRC errors). Drop it before sniffing:
> ```
> echo 1 | sudo tee /sys/bus/usb-serial/devices/ttyUSB0/latency_timer
> ```
> The CRC-resync parser recovers the boundaries either way, but a low latency
> timer also makes real-time framing reliable.
