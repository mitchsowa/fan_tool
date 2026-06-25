# commission.fan - COPRA EC fan commissioning & acceptance test
#
# Run with:   fan_tool --port /dev/ttyUSB0 scripts/commission.fan
# (On Windows: fan_tool.exe --port COM3 scripts\commission.fan)
#
# Connection parameters can be set here or on the command line. The values
# below match the COPRA factory defaults (115200 8N1, address 247).

port    /dev/ttyUSB0 115200
address 247
timeout 600
retries 2
connect

print === 1. Identify the unit ===
identify

print === 2. Baseline status (should be idle, no faults) ===
status
expect mc_state 0 0          # 0 = IDLE before we command anything
expect faults1  0 0          # no active faults
expect bus_voltage 100 800   # DC bus present and in a sane range

print === 3. Configure control channel ===
direction std                # COPRA only supports STD/CCW
forcemodbus                  # make Modbus the active demand source

print === 4. Low-speed run test (1000 RPM) ===
setspeed 1000
start
wait 8                       # allow ramp + settle
status
expect mc_state 6 6          # 6 = RUN
expect speed 800 1200        # measured speed near command
expect faults1 0 0           # still no faults

print === 5. Mid-speed run test (2500 RPM) ===
setspeed 2500
wait 8
status
expect speed 2200 2800
expect faults1 0 0

print === 6. Stop the fan ===
stop
wait 6
status
expect speed 0 200           # spun down

print === 7. (Optional) persist settings to flash ===
# Uncomment to save the configured settings so they survive a power cycle.
# Note: command speed / start are NOT persisted by design; only configuration
# registers such as direction, priorities and Modbus settings are saved.
# save

print === Commissioning sequence complete ===
disconnect
