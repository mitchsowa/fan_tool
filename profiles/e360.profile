# e360 product profile  (COPRA EC plenum fan)
#
# Loaded automatically by:  program e360   (the tool looks for
# profiles/e360.profile before falling back to the built-in defaults).
# Edit the values below to match the e360 application spec - this is the
# single place to change them; no rebuild needed.

name        = e360
description = e360 plenum fan - COPRA EC, 19200 8E1, Modbus address 11

# Communication settings the e360 runs at on the RS485 bus.
# (These take effect after a power cycle once programmed.)
comm.baud    = 19200
comm.parity  = even
comm.address = 11

# Persist the programmed registers to flash so they survive a power cycle.
save_to_flash = true

# --- Registers to program: set <register> = <value>   ( # optional note ) ---
# Communication (applied after power cycle):
set modbus_baud      = 19200   # RS485 bus baud rate
set modbus_parity    = 2       # 0=none 1=odd 2=even
set modbus_stop_bits = 1
set modbus_address   = 11      # Modbus follower address

# Operational defaults (starting point - verify for the e360 application):
set direction       = 9        # 9 = STD/CCW (COPRA supports STD only)
set modbus_priority = 1        # Modbus is the highest-priority demand source
set hb_timeout      = 20       # Modbus loss (heartbeat) timeout, seconds
