# program_e360.fan - Program a fan with the e360 product defaults.
#
# Run with:  fan_tool --port /dev/ttyUSB0 scripts/program_e360.fan
#
# autoconnect tries the factory default (115200 8N1, addr 247) first, then the
# e360 operating settings (19200 8E1, addr 11) - so it works whether the fan is
# fresh from the factory or already partly configured.

port /dev/ttyUSB0
autoconnect

print === Identify before programming ===
identify

print === Program e360 defaults (comm + operational), save to flash ===
program e360

# After programming, the comm settings only take effect on a power cycle.
# Power-cycle the fan, then re-run with autoconnect to verify it comes up at
# 19200 8E1, address 11:
#     fan_tool --port /dev/ttyUSB0 --autoconnect -i
disconnect
