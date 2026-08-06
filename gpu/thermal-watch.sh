#!/bin/bash
# thermal-watch.sh - monitor board temps, alert + auto-kill runaway GPU/render
# processes before the PowerVR board overheats. Installed as a systemd service.
#
# Thresholds (match kernel trip points):
#   WARN    = 80 C   -> log a warning
#   ALERT   = 95 C   -> log, wall, and start aggressive action
#   KILL    = 105 C  -> kill GPU/render/compositor processes to shed load
#   CRIT    = 110 C  -> kernel critical trip point (thermal shutdown)
#
# Cooling action at ALERT: repeatedly drop cpu/GPU governor to powersave and
# kill the hottest rendering process until temps fall below ALERT.
set -u
WARN=80
ALERT=95
KILL=105
LOG=/var/log/thermal-watch.log
RUNFILE=/run/thermal-watch.pid

ZONES=(
  0:cpub
  3:cpul
  4:gpu
)
get_temp() { # $1=zone index -> degrees C
  local t
  t=$(cat /sys/class/thermal/thermal_zone$1/temp 2>/dev/null) || return 1
  echo $((t / 1000))
}
now() { date '+%F %T'; }

log() { echo "$(now) $*" >> "$LOG"; }

max_temp() {
  local m=0 v
  for z in 0 3 4; do v=$(get_temp $z) && [ "$v" -gt "$m" ] && m=$v; done
  echo $m
}

# Kill hottest of the known GPU/render process families
kill_hot_render() {
  local p
  p=$(ps -eo pid,pcpu,comm --sort=-pcpu | grep -E 'pvr_weston|weston|minecraft|java|PrismLauncher|Xvfb|swrast' | head -5 | awk 'NR==1{print $1}')
  if [ -n "${p:-}" ]; then
    log "KILL_ACTION: killing hottest render proc pid=$p"
    kill -9 "$p" 2>>"$LOG" || true
  else
    log "KILL_ACTION: no render proc to kill; scaling down governor"
    for g in /sys/devices/system/cpu/cpufreq/policy*/scaling_governor; do
      echo powersave > "$g" 2>/dev/null || true
    done
  fi
}

log "thermal-watch started (WARN=${WARN}C ALERT=${ALERT}C KILL=${KILL}C)"
while true; do
  T=$(max_temp)
  if [ "$T" -ge "$KILL" ]; then
    log "CRITICAL_TEMP=${T}C"
    kill_hot_render
    # keep shedding heat: sleep briefly then re-check
  elif [ "$T" -ge "$ALERT" ]; then
    log "ALERT_TEMP=${T}C (>= ${ALERT}C)"
    wall "ALERT: board temp ${T}C - runaway process, cooling now" 2>/dev/null || true
    kill_hot_render
  elif [ "$T" -ge "$WARN" ]; then
    log "WARN_TEMP=${T}C"
  fi
  sleep 5
done
