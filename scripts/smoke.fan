# smoke.fan - Minimal connectivity check.
# Confirms the tool can talk to the fan and read its identity + status.

port    /dev/ttyUSB0 115200
address 247
connect
identify
status
disconnect
