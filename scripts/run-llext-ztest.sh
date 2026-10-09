#!/usr/bin/env bash
# Build and run the ExtensionLoaderCluster ztest suite
# (llext/tests/extension_value) under QEMU.
#
# Unlike run-qemu-macos.sh this needs no sudo: the suite calls the cluster's
# handlers directly, so there's no Matter traffic and QEMU's unprivileged
# user-mode networking is enough to satisfy the e1000 driver. The rest of the
# QEMU command line (CPU flags, pinned PCI slots) deliberately matches
# run-qemu-macos.sh -- see the comments there for why each one matters.
#
# `west build -t run` isn't usable here: Zephyr's own QEMU invocation for this
# board wants a Linux tap interface.
#
# Exits 0 only if ztest reports "PROJECT EXECUTION SUCCESSFUL". Full console
# output is left in $BUILD_DIR/console.log.
#
# Usage: run-llext-ztest.sh [BUILD_DIR]
# Env: CHIP_ROOT (default ~/src/connectedhomeip-worktree-zephyr), WEST
#      (default ~/src/zephyrproject/zephyr/.venv/bin/west), TIMEOUT (seconds
#      to wait for the suite once QEMU is up, default 120).
set -uo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${1:-$REPO_ROOT/build-llext-test}"
CHIP_ROOT="${CHIP_ROOT:-$HOME/src/connectedhomeip-worktree-zephyr}"
WEST="${WEST:-$HOME/src/zephyrproject/zephyr/.venv/bin/west}"
TIMEOUT="${TIMEOUT:-120}"
LOG="$BUILD_DIR/console.log"

# activate.sh prints a harmless arm-none-eabi-gcc check failure; see README.md.
set +u
source "$CHIP_ROOT/.environment/activate.sh" >/dev/null 2>&1
set -u
export CHIP_ROOT

# west build must run from inside the Zephyr workspace.
(cd "$(dirname "$WEST")/../../.." && "$WEST" build -b qemu_x86 "$REPO_ROOT/llext/tests/extension_value" -d "$BUILD_DIR") || exit 1

DISK_IMAGE="$BUILD_DIR/test-disk.img"
# Also drop the previous run's console log, or the verdict check below could
# match stale output before QEMU truncates the file.
rm -f "$DISK_IMAGE" "$LOG"
truncate -s 16M "$DISK_IMAGE"

"$(brew --prefix qemu)/bin/qemu-system-i386" \
	-m 32 -cpu qemu32,+nx,+pae,+clflush -machine q35 \
	-device isa-debug-exit,iobase=0xf4,iosize=0x04 -machine acpi=off \
	-display none -monitor none -serial "file:$LOG" -no-reboot \
	-netdev user,id=n1 -device e1000,netdev=n1,addr=0x2 \
	-drive "file=$DISK_IMAGE,if=none,format=raw,id=blk0" -device virtio-blk-pci,drive=blk0,addr=0x4 \
	-kernel "$BUILD_DIR/zephyr/zephyr.elf" &
QEMU_PID=$!
trap 'kill "$QEMU_PID" 2>/dev/null' EXIT

# ztest doesn't power QEMU off when it's done, so watch the console for its
# verdict (or a fatal error) instead of waiting for QEMU to exit.
for _ in $(seq "$TIMEOUT"); do
	grep -q -e 'PROJECT EXECUTION' -e 'ZEPHYR FATAL ERROR' "$LOG" 2>/dev/null && break
	kill -0 "$QEMU_PID" 2>/dev/null || break
	sleep 1
done

cat "$LOG"
grep -q 'PROJECT EXECUTION SUCCESSFUL' "$LOG"
