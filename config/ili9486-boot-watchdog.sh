#!/bin/bash
# ILI9486 boot failure watchdog
# If the system fails to boot (doesn't reach multi-user) multiple times,
# this rolls back the ILI9486 changes.

COUNTER_FILE=/var/lib/ili9486-boot-count
MAX_FAILURES=3
SUCCESS_MARKER=/var/lib/ili9486-boot-ok

# If we're running at boot (before multi-user), increment the counter
case "$1" in
  boot-start)
    COUNT=$(cat $COUNTER_FILE 2>/dev/null || echo 0)
    COUNT=$((COUNT + 1))
    echo $COUNT > $COUNTER_FILE
    if [ $COUNT -ge $MAX_FAILURES ]; then
      echo "Too many boot failures ($COUNT)! Rolling back ILI9486 changes."
      # Remove overlay from orangepiEnv.txt
      sed -i '/^overlays=ili9486$/d' /boot/orangepiEnv.txt
      # Remove module auto-load
      rm -f /etc/modules-load.d/ili9486.conf
      # Remove the overlay file
      rm -f /boot/dtb/allwinner/overlay/sun60i-a733-ili9486.dtbo
      rm -f $COUNTER_FILE
      touch $COUNTER_FILE.rolled-back
    fi
    ;;
  boot-success)
    echo 0 > $COUNTER_FILE
    touch $SUCCESS_MARKER
    ;;
  *)
    echo "Usage: $0 boot-start|boot-success"
    exit 1
    ;;
esac
