#!/usr/bin/env bash
# Run ON THE PI. Installs a custom kernel side-by-side with the stock one.
# Rollback: remove the "kernel=kernel8-custom.img" line from config.txt.
# Usage: sudo ./pi_install_kernel.sh kernel-<release>.tar.gz
set -euo pipefail

TARBALL="${1:?usage: sudo $0 kernel-<release>.tar.gz}"
BOOT=/boot/firmware
CFG="$BOOT/config.txt"
[ "$(id -u)" -eq 0 ] || { echo "run with sudo"; exit 1; }
[ -f "$CFG" ] || { BOOT=/boot; CFG=/boot/config.txt; }

TMP="$(mktemp -d)"
tar -C "$TMP" -xzf "$TARBALL"
REL="$(cat "$TMP/RELEASE")"

echo "==> Installing modules for $REL"
cp -r "$TMP/lib/modules/$REL" /lib/modules/
depmod -a "$REL"

echo "==> Installing $BOOT/kernel8-custom.img"
cp "$TMP/boot/kernel8-custom.img" "$BOOT/"
cp "$CFG" "$CFG.bak.$(date +%Y%m%d%H%M%S)"
sed -i '/^kernel=kernel8-custom.img/d' "$CFG"
printf '\n[all]\nkernel=kernel8-custom.img\n' >> "$CFG"
rm -rf "$TMP"

echo
echo "Done. Reboot, then 'uname -r' should print: $REL"
echo "Rollback: delete the kernel=kernel8-custom.img line in $CFG and reboot."
