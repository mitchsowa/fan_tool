#!/usr/bin/env bash
# Rolling live bus monitor. Captures a short window, decodes it, prints a
# compact status line per window: command writes, mc_state, speed, power,
# current, bus V. Flags any fault or state change.
cd "$(dirname "$0")"
WIN=${1:-3}
declare -A ST=( [0]=IDLE [4]=START [6]=RUN [8]=STOP [9]=STOP_IDLE [10]=FAULT_NOW [11]=FAULT_OVER )
field() { echo "$out" | grep -oE "\[$1\]=[0-9]+" | tail -1 | cut -d= -f2; }
i=0
while true; do
  i=$((i+1))
  timeout "$WIN" ./capture /dev/ttyUSB0 19200 E > live.bin 2>/dev/null
  out=$(./parse live.bin 2>/dev/null)
  ts=$(date +%H:%M:%S)
  mc=$(field 41); faults=$(field 42); speed=$(field 47); power=$(field 49); busv=$(field 46); cur=$(field 51)
  mcname=${ST[${mc:-x}]:-?}
  writes=$(echo "$out" | grep 'REQ' | grep -oE 'WRITE reg=[0-9]+ val=[0-9]+' | sort -u | tr '\n' ' ')
  printf "[%s] #%-3d mc_state=%-2s(%-9s) speed=%-4s power=%-4s cur=%-4s busV=%-3s faults1=%-2s\n" \
    "$ts" "$i" "${mc:-?}" "$mcname" "${speed:-?}" "${power:-?}" "${cur:-?}" "${busv:-?}" "${faults:-?}"
  [ -n "$faults" ] && [ "$faults" != "0" ] && echo "    ⚠️  FAULT bits set in faults1=$faults"
  echo "      writes: $writes"
done
