#!/bin/sh
# collect.sh: read-only. Writes a description of the kernel, USB devices and
# Wi-Fi support to the file given as $1. Changes nothing, needs no root, uses
# no network. Masks MAC and IP addresses; records no Wi-Fi names or passwords.
OUT="$1"
: > "$OUT" 2>/dev/null || OUT="/tmp/WiFi_Report.txt"
PORTSDIR="$(cd "$(dirname "$0")/.." && pwd)"
T=""; command -v timeout >/dev/null 2>&1 && T="timeout 5"
# hide MAC and IPv4 addresses and drop any line that mentions an SSID
mask() { sed -E 's/([0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}/xx:xx:xx:xx:xx:xx/g; s/[0-9]{1,3}(\.[0-9]{1,3}){3}/x.x.x.x/g' | grep -vi 'ssid'; }
# lines mentioning ssid (Wi-Fi network names) are dropped entirely
sec() { printf '\n==== %s ====\n' "$1" >> "$OUT"; }
run() { { $T "$@" 2>&1 | head -n 120 | mask; } >> "$OUT"; }
have() { command -v "$1" >/dev/null 2>&1; }

{
  echo "WiFiReport 1.0  $(date)"
  echo "Read-only. Please send this file back."
} >> "$OUT"

sec "kernel"
run uname -a
[ -r /proc/version ] && head -c 400 /proc/version >> "$OUT"
sec "os"
[ -r /etc/os-release ] && head -n 6 /etc/os-release >> "$OUT"
sec "user and arch"
run id
run uname -m
sec "USB devices (ids only)"
for d in /sys/bus/usb/devices/*; do
  [ -r "$d/idVendor" ] || continue
  printf '%s %s:%s  %s | %s\n' "$(basename "$d")" "$(cat "$d/idVendor")" "$(cat "$d/idProduct")" \
    "$(cat "$d/manufacturer" 2>/dev/null)" "$(cat "$d/product" 2>/dev/null)" >> "$OUT"
done
have lsusb && { sec "lsusb"; run lsusb; }
sec "network interfaces (names only)"
ls /sys/class/net >> "$OUT" 2>&1
have iw && { sec "iw dev"; run iw dev; }
have rfkill && { sec "rfkill"; run rfkill list; }
sec "loaded modules"
if [ -r /proc/modules ]; then awk '{print $1}' /proc/modules | head -n 150 >> "$OUT"; else echo "no /proc/modules" >> "$OUT"; fi
sec "module support"
echo "modules dir: $(ls -d /lib/modules/* 2>&1 | head -n 5 | tr '\n' ' ')" >> "$OUT"
echo "kernel build dir: $(ls -d /lib/modules/"$(uname -r)"/build 2>&1)" >> "$OUT"
echo "kernel headers in /usr/src: $(ls /usr/src 2>&1 | head -n 5 | tr '\n' ' ')" >> "$OUT"
[ -d /lib/modules/"$(uname -r)" ] && { echo "-- wireless modules on disk:" >> "$OUT"; find /lib/modules/"$(uname -r)" \( -name 'cfg80211*' -o -name 'mac80211*' -o -name '*rtl*' -o -name '*rtw*' -o -name '*8821*' -o -name '*8812*' -o -name '*mt76*' -o -name '*88XX*' \) 2>/dev/null | head -n 60 >> "$OUT"; }
sec "kernel config (wireless related)"
if [ -r /proc/config.gz ]; then
  zcat /proc/config.gz 2>/dev/null | grep -E '^(# )?CONFIG_(MODULES|MODVERSIONS|MODULE_SIG|MODULE_FORCE_LOAD|LOCALVERSION|CFG80211|MAC80211|WLAN|WIRELESS|WEXT|RTL|RTW|MT76|USB_NET|USB_USBNET|RFKILL|CRYPTO_(CCM|GCM|CMAC|AES))' | head -n 120 >> "$OUT"
else
  echo "/proc/config.gz not present" >> "$OUT"
  ls /boot 2>&1 | head -n 20 >> "$OUT"
fi
sec "firmware folders"
ls /lib/firmware 2>&1 | head -n 80 >> "$OUT"
for f in rtlwifi rtw88 rtw89 mediatek rtl_bt; do [ -d "/lib/firmware/$f" ] && { echo "-- /lib/firmware/$f:" >> "$OUT"; ls "/lib/firmware/$f" | head -n 40 >> "$OUT"; }; done
sec "tools present"
for t in iw wpa_supplicant wpa_cli rfkill modprobe insmod depmod modinfo lsusb curl wget dhclient dhcpcd gcc make sudo; do
  printf '%s: %s\n' "$t" "$(command -v $t || echo -)" >> "$OUT"
done
sec "kernel log (wifi and usb lines, may be empty without root)"
{ $T dmesg 2>&1 | grep -iE 'usb|wlan|cfg80211|rtl|rtw|mt76|firmware|8821|8812' | tail -n 60 | mask; } >> "$OUT"
sec "disk"
run df -h "$PORTSDIR"
echo >> "$OUT"
echo "END OF REPORT" >> "$OUT"


exit 0
