#!/usr/bin/env bash
# Run ON THE PI (once). Installs the overlay, checks module vs running kernel.
# Usage: sudo ./pi_setup.sh [--autoload]
#   --autoload : install module into /lib/modules so it loads at every boot
set -euo pipefail
cd "$(dirname "$0")"

[ "$(id -u)" -eq 0 ] || { echo "run with sudo"; exit 1; }
BOOT=/boot/firmware
[ -d "$BOOT/overlays" ] || BOOT=/boot
CFG="$BOOT/config.txt"

echo "==> Checking files"
for f in gpio_demo.ko gpio-demo.dtbo gpio_demo_cli; do
	[ -f "$f" ] || { echo "missing $f"; exit 1; }
done
chmod +x gpio_demo_cli pi_test.sh 2>/dev/null || true

echo "==> Kernel version check"
RUN="$(uname -r)"
MOD="$(modinfo -F vermagic gpio_demo.ko | awk '{print $1}')"
echo "    running kernel : $RUN"
echo "    module built for: $MOD"
if [ "$RUN" != "$MOD" ]; then
	echo
	echo "WARNING: mismatch -> insmod will fail with 'Invalid module format'."
	echo "  Option A: install the matching kernel (scripts/kernel_build.sh on Ubuntu)"
	echo "  Option B: build on the Pi: sudo apt install linux-headers-rpi-v8 && make"
	echo
fi

echo "==> Installing overlay -> $BOOT/overlays/gpio-demo.dtbo"
cp gpio-demo.dtbo "$BOOT/overlays/"

if grep -q '^dtoverlay=gpio-demo' "$CFG"; then
	echo "    dtoverlay=gpio-demo already in $CFG"
	NEED_REBOOT=0
else
	cp "$CFG" "$CFG.bak.$(date +%Y%m%d%H%M%S)"
	printf '\n[all]\ndtoverlay=gpio-demo\n' >> "$CFG"
	echo "    added dtoverlay=gpio-demo to $CFG"
	NEED_REBOOT=1
fi

if [ "${1:-}" = "--autoload" ]; then
	echo "==> Installing module for autoload at boot"
	install -D -m 644 gpio_demo.ko "/lib/modules/$RUN/extra/gpio_demo.ko"
	depmod -a
fi

# Device node present after reboot? (overlay applied)
if [ -d /proc/device-tree/gpio-demo ]; then
	echo "==> DT node /proc/device-tree/gpio-demo present"
else
	NEED_REBOOT=1
fi

echo
if [ "$NEED_REBOOT" = 1 ]; then
	echo "Reboot now:  sudo reboot"
	echo "Then run:    sudo ./pi_test.sh"
else
	echo "Ready. Run:  sudo ./pi_test.sh"
fi
