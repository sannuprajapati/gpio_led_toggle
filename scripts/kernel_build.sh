#!/usr/bin/env bash
# Option A: build the full kernel from KDIR so the Pi runs the SAME kernel
# the module was compiled against. Output: build/kernel/ (Image + modules).
# Usage: ./scripts/kernel_build.sh [KDIR]
set -euo pipefail

KDIR="${1:-/home/sannu/linux-rpi-6.18.y/linux-rpi-6.18.y}"
OUT="$(cd "$(dirname "$0")/.." && pwd)/build/kernel"
MK="make -C $KDIR ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu-"

[ -f "$KDIR/.config" ] || $MK bcm2711_defconfig

echo "==> Building Image + modules (takes a while)"
$MK -j"$(nproc)" Image modules

REL="$($MK -s kernelrelease)"
rm -rf "$OUT"
mkdir -p "$OUT/boot"
$MK INSTALL_MOD_PATH="$OUT" INSTALL_MOD_STRIP=1 modules_install
rm -f "$OUT/lib/modules/$REL/build" "$OUT/lib/modules/$REL/source"
cp "$KDIR/arch/arm64/boot/Image" "$OUT/boot/kernel8-custom.img"
echo "$REL" > "$OUT/RELEASE"

tar -C "$OUT" -czf "$OUT/../kernel-$REL.tar.gz" boot lib RELEASE
echo
echo "Kernel $REL packaged: build/kernel-$REL.tar.gz"
echo "Copy it with:  scp build/kernel-$REL.tar.gz scripts/pi_install_kernel.sh sannu@<pi-ip>:~/"
echo "On the Pi:     sudo ./pi_install_kernel.sh kernel-$REL.tar.gz"
