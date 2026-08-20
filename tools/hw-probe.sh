#!/bin/sh
# SysMetrics hardware probe — dumps the CPU / GPU / camera / thermal / power
# sysfs the app reads, so support for a new SoC can be added. Contains only
# hardware/kernel capability info (no serials, IMEI/IMSI or personal data).
#
# Run on the device (root not required):
#   sh hw-probe.sh
# It writes  sysmetrics-hwprobe-<model>-<date>.txt  in the current directory.

MODEL=$(tr -d '\0' < /sys/firmware/devicetree/base/model 2>/dev/null | tr ' /' '__' | tr -cd 'A-Za-z0-9_.-')
[ -z "$MODEL" ] && MODEL=unknown
OUT="sysmetrics-hwprobe-${MODEL}-$(date +%Y%m%d-%H%M%S 2>/dev/null || echo run).txt"

{
sep() { echo; echo "==== $* ===="; }

sep "DEVICE"
uname -m
tr -d '\0' < /sys/firmware/devicetree/base/model 2>/dev/null; echo
tr '\0' ' ' < /sys/firmware/devicetree/base/compatible 2>/dev/null; echo

sep "CPU identity (/proc/cpuinfo)"
grep -iE "hardware|processor|cpu part|cpu implementer|cpu variant|model name|bogomips" /proc/cpuinfo | sort -u

sep "CPU frequency (policy* and per-cpu fallback)"
for p in /sys/devices/system/cpu/cpufreq/policy*; do
  [ -d "$p" ] && echo "$(basename $p): cpus=$(cat $p/affected_cpus 2>/dev/null) cur=$(cat $p/scaling_cur_freq 2>/dev/null) min=$(cat $p/cpuinfo_min_freq 2>/dev/null) max=$(cat $p/cpuinfo_max_freq 2>/dev/null) gov=$(cat $p/scaling_governor 2>/dev/null)"
done
for c in /sys/devices/system/cpu/cpu[0-9]*/cpufreq; do
  [ -d "$c" ] && echo "$(echo $c | sed 's#/sys/devices/system/cpu/##;s#/cpufreq##'): cur=$(cat $c/scaling_cur_freq 2>/dev/null) max=$(cat $c/cpuinfo_max_freq 2>/dev/null)"
done

sep "GPU: Qualcomm kgsl"
ls -1 /sys/class/kgsl/ 2>/dev/null || echo "no kgsl"
cat /sys/class/kgsl/kgsl-3d0/gpu_model 2>/dev/null

sep "GPU: devfreq nodes"
for d in /sys/class/devfreq/*; do
  [ -e "$d" ] && echo "$(basename $d): cur=$(cat $d/cur_freq 2>/dev/null) max=$(cat $d/max_freq 2>/dev/null) compat=$(tr '\0' ' ' <$d/device/of_node/compatible 2>/dev/null)"
done

sep "GPU: ARM Mali"
ls -d /sys/class/misc/mali* /sys/devices/platform/soc/*mali* /sys/devices/platform/*mali* 2>/dev/null
cat /sys/class/misc/mali0/device/gpuinfo 2>/dev/null
for u in /sys/kernel/ged/hal/current_freqency /sys/kernel/ged/hal/gpu_utilization \
         /sys/class/misc/mali0/device/utilisation /sys/class/misc/mali0/device/utilization; do
  [ -e "$u" ] && echo "$u = $(cat $u 2>/dev/null)"
done

sep "GPU: DRM"
for c in /sys/class/drm/card*/device/uevent; do [ -e "$c" ] && echo "$(dirname $c | sed 's#/sys/class/drm/##;s#/device##'): $(grep -i DRIVER= $c 2>/dev/null)"; done

sep "CAMERA: video4linux"
for e in /sys/class/video4linux/*; do [ -e "$e" ] && echo "$(basename $e): $(cat $e/name 2>/dev/null)"; done
ls -1 /dev/video* 2>/dev/null

sep "CAMERA: Qualcomm vendor modules"
ls -1 /vendor/lib*/camera/ /odm/lib*/camera/ 2>/dev/null | grep -iE "sensormodule|\.bin" | head

sep "CAMERA: MediaTek imgsensor / mtkcam"
ls -d /proc/driver/camsensor* /sys/module/imgsensor* /sys/bus/platform/drivers/seninf* 2>/dev/null
grep -riE "imx[0-9]|ov[0-9]{4}|s5k|gc[0-9]{4}|hi[0-9]{3}|jn[0-9]" /sys/bus/i2c/devices/*/name 2>/dev/null | head

sep "THERMAL zones"
for z in /sys/class/thermal/thermal_zone*; do [ -e "$z/type" ] && echo "$(basename $z): $(cat $z/type 2>/dev/null)"; done | head -40

sep "POWER supplies (names + type only)"
for s in /sys/class/power_supply/*; do [ -e "$s/type" ] && echo "$(basename $s): type=$(cat $s/type 2>/dev/null) status=$(cat $s/status 2>/dev/null)"; done

sep "END"
} 2>&1 | tee "$OUT"

echo
echo "Saved to: $(pwd)/$OUT"
echo "Attach this file to a report — it contains no personal data."
