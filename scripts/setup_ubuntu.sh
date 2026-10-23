#!/usr/bin/env bash
# One-time Ubuntu / WSL2 setup: toolchain + prepared RPi kernel tree.
# Usage: ./scripts/setup_ubuntu.sh [KDIR]
set -euo pipefail

KDIR="${1:-/home/sannu/linux-rpi-6.18.y/linux-rpi-6.18.y}"
BRANCH="rpi-6.18.y"
MK="make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu-"

echo "==> Installing packages"
sudo apt-get update -qq
sudo apt-get install -y gcc-aarch64-linux-gnu make bc bison flex \
	libssl-dev libelf-dev device-tree-compiler git kmod openssh-client

if [ ! -f "$KDIR/Makefile" ]; then
	echo "==> Cloning raspberrypi/linux ($BRANCH) into $KDIR"
	mkdir -p "$(dirname "$KDIR")"
	git clone --depth 1 -b "$BRANCH" https://github.com/raspberrypi/linux.git "$KDIR"
fi

cd "$KDIR"
if [ ! -f .config ]; then
	echo "==> bcm2711_defconfig (64-bit config used for Pi 3/3B+/4)"
	$MK bcm2711_defconfig
fi

# Full build, not just modules_prepare: out-of-tree modules need
# Module.symvers, otherwise modpost fails with "undefined!" symbols.
if [ ! -f Module.symvers ]; then
	echo "==> Building kernel Image + modules (first time: 20-60 min)"
	$MK -j"$(nproc)" Image modules
else
	echo "==> Module.symvers present, skipping kernel build"
fi

echo
echo "Kernel tree ready: $KDIR"
echo "Module will be built for: $($MK -s kernelrelease)"
echo "Compare with 'uname -r' on the Pi. If they differ, see README (Option A / B)."
