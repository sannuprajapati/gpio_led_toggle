#!/usr/bin/env bash
# Run ON THE PI. Loads the driver, shows state, runs the CLI test suite.
# Usage: sudo ./pi_test.sh [--keep]     (--keep: leave module loaded)
set -uo pipefail
cd "$(dirname "$0")"

[ "$(id -u)" -eq 0 ] || { echo "run with sudo"; exit 1; }
SYS=/sys/bus/platform/devices/gpio-demo

step() { echo; echo "==> $*"; }

step "Device tree node"
if [ ! -d /proc/device-tree/gpio-demo ]; then
	echo "FAIL: /proc/device-tree/gpio-demo missing -> overlay not applied."
	echo "      run: sudo ./pi_setup.sh && sudo reboot"
	exit 1
fi
echo "compatible = $(tr -d '\0' < /proc/device-tree/gpio-demo/compatible)"

step "Loading module"
lsmod | grep -q '^gpio_demo' && rmmod gpio_demo
if ! insmod ./gpio_demo.ko; then
	echo "FAIL: insmod. Check: dmesg | tail ; uname -r vs modinfo -F vermagic gpio_demo.ko"
	exit 1
fi
sleep 0.5
dmesg | grep -i 'gpio-demo' | tail -3

if [ ! -d "$SYS" ] || [ ! -e "$SYS/led" ]; then
	echo "FAIL: driver did not bind ($SYS/led missing). dmesg:"
	dmesg | tail -10
	exit 1
fi

step "GPIO ownership (gpiolib)"
gpioinfo 2>/dev/null | grep -E '"GPIO(17|27)"' || echo "(install gpiod for gpioinfo)"
command -v pinctrl >/dev/null && pinctrl get 17,27

step "Interrupt"
grep gpio-demo-btn /proc/interrupts || echo "IRQ not listed"

step "Quick visual check: 3 blinks"
./gpio_demo_cli blink 3 300

step "Automated test (press the button when asked)"
./gpio_demo_cli test
RC=$?

step "Interrupt count after test"
grep gpio-demo-btn /proc/interrupts
echo "accepted presses: $(cat $SYS/presses)"

if [ "${1:-}" != "--keep" ]; then
	step "Unloading module"
	rmmod gpio_demo && echo "unloaded (LED switched off by devm action)"
fi

echo
[ $RC -eq 0 ] && echo "=== ALL TESTS PASSED ===" || echo "=== $RC TEST(S) FAILED ==="
exit $RC
