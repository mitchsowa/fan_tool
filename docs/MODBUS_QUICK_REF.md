# COPRA EC Fan — Modbus Quick Reference

The essential registers for **commanding** the fan and reading **feedback**.
Addresses are the raw Modbus protocol addresses placed on the wire (as used by
`fan_tool`). For the full map see [REGISTERS.md](REGISTERS.md).

- **Holding registers** — read with FC `0x03`, write with FC `0x06` (single) /
  `0x10` (multiple).
- **Input registers** — read-only, FC `0x04`.
- **Engineering value = raw × scale** (and `raw = round(value / scale)` to write).

## Control — write (holding registers)

| Function       | Register | Type | Unit / Scale         | Values                          |
|----------------|---------:|------|----------------------|---------------------------------|
| Start / Stop   |   35947  | enum | —                    | `1` = START, `0` = STOP         |
| Command speed  |   35945  | u16  | RPM (×1), 0–6000     | target speed                    |
| Command demand |   35946  | u16  | % (×0.01), raw 0–10000 | target demand; overrides speed |

> To take control, the demand source must be MODBUS: set **`demand_source`
> (register `34409`) = `5`**, then `start` = `1`. Without this the motor
> follows its local source and ignores the command above.

## Feedback — read (input registers)

| Quantity          | Register | Type     | Unit / Scale | Notes                         |
|-------------------|---------:|----------|--------------|-------------------------------|
| DC bus voltage    |    46    | u16      | V (×1)       |                               |
| Phase current Ia  |    51    | u16      | mA (×0.01)   | peak motor phase current      |
| Phase current Ib  |    52    | u16      | mA (×0.01)   | peak motor phase current      |
| Input power       |    49    | s16      | W (×1)       | calculated input power        |
| Actual speed      |    47    | s16      | RPM (×1)     | measured speed feedback       |
| Status word       |    41    | enum     | —            | motor-control state (below)   |
| Alarms / faults 1 |    42    | bitfield | —            | active faults (bits below)    |
| Alarms / faults 2 |    43    | bitfield | —            | safety-core faults            |

### Status word — `mc_state` (register 41)

| Value | State        |
|------:|--------------|
| 0     | IDLE         |
| 4     | START        |
| 6     | RUN          |
| 8     | STOP         |
| 9     | STOP_IDLE    |
| 10    | FAULT_NOW    |
| 11    | FAULT_OVER   |

### Alarms — `faults1` bitfield (register 42)

| Bit | Alarm                 | Bit | Alarm                  |
|----:|-----------------------|----:|------------------------|
| 0   | FOC_Duration          | 6   | Input_Loss_of_Phase    |
| 1   | Under_Voltage         | 7   | Output_Loss_of_Phase   |
| 2   | Over_Voltage          | 8   | Over_Current           |
| 3   | Over_Temperature      | 9   | Safety_Core            |
| 4   | Speed_Feedback        | 10  | Internal_Comm_Loss     |
| 5   | Startup               | 11  | Software_Error         |

A non-zero `faults1` means one or more bits are set; `faults2` carries the
safety-core fault bits. `0` on both = no active alarms.
</content>
</invoke>
