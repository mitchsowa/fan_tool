# COPRA Modbus Register Map (subset used by this tool)

Source: *COPRA Products Modbus Specification – User Registers, Release 1.0*.

- **Value convention:** `engineering value = raw × scale`. When writing,
  `raw = round(value / scale)`.
- **Space:** `Holding` registers are read/write (FC 03/06/16); `Input`
  registers are read-only (FC 04).
- Script/shell **name** is the identifier used by `read`, `write`, `expect`.

## Fan control (Register Group 3, holding)

| Name | Addr | Scale | Unit | Notes |
|------|------|-------|------|-------|
| `cmd_speed` | 35945 | 1 | RPM | Command speed, 0–6000 |
| `cmd_demand` | 35946 | 0.01 | % | Command demand, 0–100%. **Overrides `cmd_speed`** |
| `start` | 35947 | 1 | — | 1 = START, 0 = STOP |
| `direction` | 35948 | 1 | — | 9 = STD/CCW, 6 = REVERSE (COPRA = STD only) |

> These are runtime-only and are **not** saved across a power cycle.

## Demand multiplexer (holding + input)

| Name | Addr | Space | Notes |
|------|------|-------|-------|
| `demand_source` | 34409 | Holding | 0 = priority, 1 = 0-10V, 2 = 4-20mA, 3 = DI, 4 = PWM, 5 = MODBUS |
| `modbus_priority` | 34412 | Holding | 1 = highest priority (default) |
| `default_demand` | 34418 | Holding | % used on power up if non-zero |
| `demand_value` | 34345 | Input | % currently applied to the motor |
| `active_source` | 34346 | Input | Source actually driving the fan |

## Drive metering / status (input)

| Name | Addr | Type | Unit | Notes |
|------|------|------|------|-------|
| `mc_state` | 41 | enum | — | 0 IDLE, 4 START, 6 RUN, 8 STOP, 9 STOP_IDLE, 10 FAULT_NOW, 11 FAULT_OVER |
| `faults1` | 42 | bits | — | b0 FOC, b1 UnderV, b2 OverV, b3 OverTemp, b4 SpeedFb, b5 Startup, b6 InPhaseLoss, b7 OutPhaseLoss, b8 OverCurrent, b9 SafetyCore, b10 IntCommLoss, b11 SoftwareErr |
| `faults2` | 43 | bits | — | Safety-core fault detail |
| `app_state` | 44 | enum | — | Application state machine |
| `act_direction` | 45 | enum | — | 9 = STD, 6 = REVERSE |
| `bus_voltage` | 46 | u16 | V | DC bus voltage |
| `speed` | 47 | s16 | RPM | Measured speed feedback |
| `torque` | 48 | s16 | pu | Measured torque |
| `power` | 49 | s16 | W | Input power |
| `ipm_temp` | 50 | s16 | °C | IPM temperature |
| `current_a` | 51 | u16 ×0.01 | mA | Peak phase current Ia |
| `current_b` | 52 | u16 ×0.01 | mA | Peak phase current Ib |
| `shaft_power` | 65 | s16 | W | Shaft power |

## Identity (input)

| Name | Addr | Notes |
|------|------|-------|
| `fw_minor` / `fw_median` / `fw_major` | 554 / 555 / 556 | Firmware rev (ASCII), forms `VV.XX.YY` |
| `app_fw_version` | 35369 | Application firmware version |
| `product_variant` | 570 | Product / variant code |

## Modbus settings (holding) — applied after power cycle

| Name | Addr | Scale | Notes |
|------|------|-------|-------|
| `modbus_baud` | 33897 | 100 | e.g. raw 1152 = 115200 baud |
| `modbus_address` | 33902 | 1 | Unit address 1–247 |
| `hb_timeout` | 33905 | 1 | Modbus loss (heartbeat) timeout, s |
| `loss_demand` | 33906 | 0.01 | Demand % on Modbus loss |

## Flash save (per spec §3.4)

To persist holding-register settings: write **1**
(`FLASH_WRITE_RAM2FLASH_USER_SETTINGS_CMD`) to the command register, then poll
the status register until **8** (`FLASH_SETTINGS_WRITE_COMPLETE`); **2** =
`FLASH_ERROR`. Repeat for both app and drive. The `save` command does this.

| Name | Addr | Space |
|------|------|-------|
| `app_flash_cmd` | 35689 | Holding |
| `app_flash_status` | 35625 | Input |
| `drive_flash_cmd` | 4969 | Holding |
| `drive_flash_status` | 4905 | Input |

> ⚠️ Flash endurance is ~100,000 writes — only `save` when settings must
> survive a power cycle.
