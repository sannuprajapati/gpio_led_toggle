# gpio_led_toggle

A Linux GPIO consumer driver for the Raspberry Pi 3B+. Pressing the button toggles the LED through an IRQ, and the LED can also be driven from userspace through sysfs. The project includes a CLI test app. It cross-builds on Ubuntu or WSL2 and is tested on the Pi.

```
gpio_led_toggle/
├── driver/gpio_demo.c          kernel module (gpiod API, threaded IRQ, sysfs)
├── driver/Kbuild
├── dts/gpio-demo-overlay.dts   DT overlay: pins, pull-up, active-low
├── app/gpio_demo_cli.c         CLI: on/off/toggle/status/blink/watch/test
├── scripts/
│   ├── setup_ubuntu.sh         toolchain + clone/build RPi kernel tree (once)
│   ├── kernel_build.sh         Option A: package matching kernel for the Pi
│   ├── pi_install_kernel.sh    Option A: install it on the Pi (side-by-side)
│   ├── pi_setup.sh             Pi: install overlay, version check
│   └── pi_test.sh              Pi: load, inspect, run test suite, unload
└── Makefile                    auto: cross on x86_64, native on aarch64
```

## 1. Wiring

| Part   | GPIO   | Pin | Connection                                           |
|--------|--------|-----|------------------------------------------------------|
| LED    | GPIO17 | 11  | pin 11 → 330 Ω → LED anode, LED cathode → GND (pin 9) |
| Button | GPIO27 | 13  | pin 13 → button → GND (pin 14). No resistor: internal pull-up |

GPIO17 must be free. Disconnect the DHT11 if it is still on pin 11, and stop any Python or lgpio script that is holding the pin.

## 2. Build on Ubuntu / WSL2

```bash
cd ~/gpio_led_toggle                 # keep it in ~/, not /mnt/c/
./scripts/setup_ubuntu.sh            # once: toolchain + kernel tree + full build
make                                 # -> build/gpio_demo.ko, gpio-demo.dtbo, gpio_demo_cli
make info                            # kernel release the .ko is built for
```

The kernel tree path defaults to `/home/sannu/linux-rpi-6.18.y/linux-rpi-6.18.y`. You can override it with `make KDIR=/path`.

## 3. The kernel version must match

`insmod` loads a module only when its version magic equals `uname -r` on the Pi. The tree builds `6.18.58-v8+`, while stock Pi OS runs a Debian kernel such as `6.12.x+rpt-rpi-v8`. To find out which case you are in, run `uname -r` on the Pi.

**Option A: run your own kernel (cross-compile flow).** The stock kernel stays installed.
```bash
# Ubuntu
./scripts/kernel_build.sh
scp build/kernel-*.tar.gz scripts/pi_install_kernel.sh sannu@<pi-ip>:~/
# Pi
sudo ./pi_install_kernel.sh kernel-6.18.58-v8+.tar.gz && sudo reboot
uname -r                             # 6.18.58-v8+
```
To roll back, delete the `kernel=kernel8-custom.img` line from `/boot/firmware/config.txt` and reboot.

**Option B: build on the Pi against the stock kernel.**
```bash
scp -r ~/gpio_led_toggle sannu@<pi-ip>:~/
# Pi
sudo apt install -y linux-headers-rpi-v8 device-tree-compiler
cd ~/gpio_led_toggle && make          # Makefile detects aarch64 -> native build
cp scripts/pi_*.sh build/ && cd build
```

## 4. Deploy (Option A)

```bash
make deploy PI=sannu@<pi-ip>          # copies .ko, .dtbo, cli, pi_*.sh to ~/gpio_led_toggle
```

## 5. Set up and test on the Pi

```bash
cd ~/gpio_led_toggle
sudo ./pi_setup.sh                    # overlay -> /boot/firmware/overlays, config.txt
sudo reboot
sudo ./pi_test.sh                     # press the button when asked
```

What `pi_test.sh` checks:

1. The DT node `/proc/device-tree/gpio-demo` exists, which means the overlay was applied.
2. `insmod` succeeds and the driver binds, so sysfs `led`, `button` and `presses` appear.
3. gpiolib shows GPIO17 and GPIO27 claimed by `gpio-demo`, and `pinctrl` shows the pull-up.
4. The IRQ is listed in `/proc/interrupts`.
5. The LED blinks 3 times.
6. The `gpio_demo_cli test` suite passes: LED readback, `EINVAL` on bad input, read-only button, idle level, and IRQ toggle plus debounce.
7. `rmmod` works and the LED is switched off by the devm action.

Expected end: `=== ALL TESTS PASSED ===`. Use `--keep` to leave the module loaded.

## 6. Manual use

```bash
sudo ./gpio_demo_cli on | off | toggle | status
sudo ./gpio_demo_cli blink 10 200
sudo ./gpio_demo_cli watch            # live view, press the button
cat /sys/bus/platform/devices/gpio-demo/presses
dmesg -w | grep gpio-demo
```

To load the driver automatically at boot, run `sudo ./pi_setup.sh --autoload`. udev matches the DT compatible string `sannu,gpio-demo`.

## Troubleshooting

| Symptom | Cause / fix |
|---|---|
| `modpost: ... undefined!` during build | Kernel tree only had `modules_prepare`. Run `setup_ubuntu.sh`, which does the full build and creates `Module.symvers` |
| `insmod: Invalid module format` | vermagic ≠ `uname -r`. Use Option A or B (section 3) |
| `pi_test`: DT node missing | Overlay not applied. Check that `dtoverlay=gpio-demo` is in config.txt and the `.dtbo` is in `overlays/`, then reboot |
| probe fails `-EBUSY` / `-16` in dmesg | GPIO17/27 already claimed. Check `gpioinfo`, kill the lgpio script, and look for overlay clashes |
| Button reads 1 when idle | Pull-up not applied. `pinctrl get 27` should show `pu` |
| One press toggles twice | Bouncy switch. Increase `SETTLE_MS` or `DEBOUNCE_MS` in the driver |
