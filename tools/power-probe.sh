#!/bin/sh
# SysMetrics power probe — answers one question: does this device's fuel gauge
# report a current that reacts to CPU load, how fast, and by how much. That is
# what decides whether per-process milliamps can be estimated at all.
#
# Reads only. It touches the power-supply class, cpufreq and /proc/stat, and it
# skips any attribute that is writable by someone, because vendor charger
# directories hang command nodes in among the readings.
#
# Run on the device (root not required, more is visible with it):
#   sh power-probe.sh
# It writes  sysmetrics-power-<model>-<date>.txt  in the current directory.
#
# It puts all cores under load for 20 seconds. The phone gets warm, nothing else.
# UNPLUG THE CHARGER FIRST — on the cable the gauge measures the cell, not the
# device, and the run says nothing.

MODEL=$(tr -d '\0' < /sys/firmware/devicetree/base/model 2>/dev/null | tr ' /' '__' | tr -cd 'A-Za-z0-9_.-')
[ -z "$MODEL" ] && MODEL=unknown
OUT="sysmetrics-power-${MODEL}-$(date +%Y%m%d-%H%M%S 2>/dev/null || echo run).txt"

# Subsecond sleep is not in POSIX; fall back to whole seconds where it is absent.
STEP=0.25
sleep $STEP 2>/dev/null || STEP=1

{
sep() { echo; echo "==== $* ===="; }

sep "DEVICE"
uname -rm
tr -d '\0' < /sys/firmware/devicetree/base/model 2>/dev/null; echo
echo "sample step: ${STEP}s"

sep "POWER SUPPLIES (readable attributes only)"
for s in /sys/class/power_supply/*; do
  [ -d "$s" ] || continue
  echo "-- $(basename "$s")"
  for f in "$s"/*; do
    [ -f "$f" ] || continue
    case $(basename "$f") in uevent|*_cmd|en_*) continue;; esac
    # A node carrying a write bit for anyone is a control, not a reading, and
    # vendor charger directories hang those in among the measurements. Name it,
    # do not read it -- reading some of them does work.
    case $(stat -c %a "$f" 2>/dev/null) in
      444|440|400|"") printf "   %-30s %s\n" "$(basename "$f")" "$(head -1 "$f" 2>/dev/null)" ;;
      *)              printf "   %-30s [writable, not read]\n" "$(basename "$f")" ;;
    esac
  done
done

sep "CHARGER STATE (the run is only meaningful unplugged)"
plugged=0
for s in /sys/class/power_supply/*; do
  [ -f "$s/online" ] || continue
  o=$(cat "$s/online" 2>/dev/null)
  echo "$(basename "$s")/online = $o"
  [ "$o" = "1" ] && plugged=1
done
echo "battery/status = $(cat /sys/class/power_supply/battery/status 2>/dev/null)"
[ "$plugged" = "1" ] && echo "!! CHARGER CONNECTED — unplug and run again, these numbers do not answer the question"

sep "CPU CLUSTERS"
for p in /sys/devices/system/cpu/cpufreq/policy*; do
  [ -d "$p" ] || continue
  echo "$(basename "$p"): cpus=$(cat "$p"/related_cpus 2>/dev/null) max=$(cat "$p"/cpuinfo_max_freq 2>/dev/null) gov=$(cat "$p"/scaling_governor 2>/dev/null)"
done

# Which node carries the current, and in which unit. Both spellings exist, and
# MediaTek's legacy one counts milliamps where the standard counts microamps.
CUR=/sys/class/power_supply/battery/current_now
[ -f "$CUR" ] || CUR=/sys/class/power_supply/battery/BatteryAverageCurrent
VOL=/sys/class/power_supply/battery/voltage_now
[ -f "$VOL" ] || VOL=/sys/class/power_supply/battery/batt_vol
echo
echo "current node: $CUR"
echo "voltage node: $VOL"

# One line per sample: seconds, raw current, raw voltage, /proc/stat totals to
# derive load offline, and the per-cluster frequency.
FREQS=""
for p in /sys/devices/system/cpu/cpufreq/policy*; do
  [ -d "$p" ] && FREQS="$FREQS $p/scaling_cur_freq"
done
sample() {
  printf "%s %s %s %s |" "$(date +%s.%N 2>/dev/null || date +%s)" \
    "$(cat $CUR 2>/dev/null)" "$(cat $VOL 2>/dev/null)" "$(head -1 /proc/stat)"
  for f in $FREQS; do printf " %s" "$(cat $f 2>/dev/null)"; done
  echo
}

sep "IDLE — 40 samples, before any load"
i=0; while [ $i -lt 40 ]; do sample; sleep $STEP; i=$((i+1)); done

sep "LOAD STEP — 10s idle, 20s all cores busy, 15s idle"
NCPU=$(grep -c ^processor /proc/cpuinfo 2>/dev/null || echo 4)
echo "# busy loops: $NCPU"
i=0; while [ $i -lt 40 ]; do sample; sleep $STEP; i=$((i+1)); done
echo "# LOAD ON"
LOADPIDS=""
i=0; while [ $i -lt "$NCPU" ]; do
  ( while : ; do : ; done ) & LOADPIDS="$LOADPIDS $!"
  i=$((i+1))
done
i=0; while [ $i -lt 80 ]; do sample; sleep $STEP; i=$((i+1)); done
echo "# LOAD OFF"
for p in $LOADPIDS; do kill "$p" 2>/dev/null; done
wait 2>/dev/null
i=0; while [ $i -lt 60 ]; do sample; sleep $STEP; i=$((i+1)); done

sep "THERMAL after the run"
for z in /sys/class/thermal/thermal_zone*; do
  [ -d "$z" ] && echo "$(cat "$z"/type 2>/dev/null) = $(cat "$z"/temp 2>/dev/null)"
done

sep "WAKEUP SOURCES (needs root; who keeps the device awake)"
head -1 /sys/kernel/debug/wakeup_sources 2>/dev/null || echo "not readable"
sort -k6 -rn /sys/kernel/debug/wakeup_sources 2>/dev/null | head -20

sep "SUSPEND"
cat /sys/power/suspend_stats/success /sys/power/suspend_stats/fail 2>/dev/null
} > "$OUT" 2>&1

echo "written: $OUT"
