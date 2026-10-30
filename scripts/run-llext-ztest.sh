#!/usr/bin/env bash
# Build and run one of the ExtensionLoaderCluster ztest suites under QEMU:
#   2_llext/tests/extension_value     (default)
#   3_llext_lvgl/tests/extension_ui
#   4_llext_dynamic/tests/dynamic_endpoints
#   6_nrf52840_signed/tests/signature
#
# Unlike run-qemu-macos.sh this needs no sudo: the suites call the cluster's
# handlers directly, so there's no Matter traffic and QEMU's unprivileged
# user-mode networking is enough to satisfy the e1000 driver. The rest of the
# QEMU command line (CPU flags, pinned PCI slots, ramfb for CONFIG_DISPLAY=y
# builds) deliberately matches run-qemu-macos.sh -- see the comments there
# for why each one matters.
#
# `west build -t run` isn't usable here: Zephyr's own QEMU invocation for this
# board wants a Linux tap interface.
#
# Exits 0 only if ztest reports "PROJECT EXECUTION SUCCESSFUL". Full console
# output is left in $BUILD_DIR/console.log.
#
# Screenshots: whenever the test prints a line "SCREENSHOT <name>", this
# script saves a QEMU screendump as $BUILD_DIR/screenshots/<name>.png (the
# test is expected to pause for a second or two afterwards). No window is
# opened. Since a test can't see what LVGL actually drew, the run also fails
# if any two screenshots are identical -- tests are written so that every
# step should change the picture.
#
# Usage: run-llext-ztest.sh [TEST_DIR] [BUILD_DIR]
#   TEST_DIR   relative to the repo root (default 2_llext/tests/extension_value)
#   BUILD_DIR  default build-<sample>-test, e.g. build-llext-test
# Env: CHIP_ROOT (default ~/src/connectedhomeip-worktree-zephyr), WEST
#      (default ~/src/zephyrproject/zephyr/.venv/bin/west), TIMEOUT (seconds
#      to wait for the suite once QEMU is up, default 120).
set -uo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TEST_DIR="${1:-2_llext/tests/extension_value}"
TEST_DIR="${TEST_DIR%/}"
# Build directories are named after the sample without its number prefix:
# 3_llext_lvgl -> build-llext-lvgl-test.
SAMPLE="${TEST_DIR%%/*}"
SAMPLE="${SAMPLE#[0-9]_}"
BUILD_DIR="${2:-$REPO_ROOT/build-${SAMPLE//_/-}-test}"
CHIP_ROOT="${CHIP_ROOT:-$HOME/src/connectedhomeip-worktree-zephyr}"
WEST="${WEST:-$HOME/src/zephyrproject/zephyr/.venv/bin/west}"
TIMEOUT="${TIMEOUT:-120}"
LOG="$BUILD_DIR/console.log"
SHOTS_DIR="$BUILD_DIR/screenshots"
MON_SOCK="$BUILD_DIR/mon.sock"

# activate.sh prints a harmless arm-none-eabi-gcc check failure; see README.md.
set +u
source "$CHIP_ROOT/.environment/activate.sh" >/dev/null 2>&1
set -u
export CHIP_ROOT
# zap-cli, for suites that build a data model (4_llext_dynamic); see README.md.
export PATH="$CHIP_ROOT/.environment/extra-cipd/zap:$PATH"

# Zephyr's add_llext_target() makes the step that turns an extension's object
# file into its .llext depend on the compile *target* only, not on the object
# file itself, so after editing an extension's source the object is rebuilt
# but the embedded .llext is not: the suite would run against the old
# extension. Dropping the packaged extensions forces that step to rerun.
rm -rf "$BUILD_DIR/llext"

# west build must run from inside the Zephyr workspace.
(cd "$(dirname "$WEST")/../../.." && "$WEST" build -b qemu_x86 "$REPO_ROOT/$TEST_DIR" -d "$BUILD_DIR") || exit 1

# Also drop the previous run's console log and screenshots, or the checks
# below could match stale output before QEMU truncates the file.
DISK_IMAGE="$BUILD_DIR/test-disk.img"
rm -rf "$DISK_IMAGE" "$LOG" "$SHOTS_DIR"
truncate -s 16M "$DISK_IMAGE"
mkdir -p "$SHOTS_DIR"

DISPLAY_ARGS=()
if grep -q '^CONFIG_DISPLAY=y' "$BUILD_DIR/zephyr/.config"; then
	DISPLAY_ARGS=(-device ramfb -vga none)
fi

"$(brew --prefix qemu)/bin/qemu-system-i386" \
	-m 32 -cpu qemu32,+nx,+pae,+clflush -machine q35 \
	-device isa-debug-exit,iobase=0xf4,iosize=0x04 -machine acpi=off \
	-display none -serial "file:$LOG" -no-reboot \
	-monitor "unix:$MON_SOCK,server=on,wait=off" \
	${DISPLAY_ARGS[@]+"${DISPLAY_ARGS[@]}"} \
	-netdev user,id=n1 -device e1000,netdev=n1,addr=0x2 \
	-drive "file=$DISK_IMAGE,if=none,format=raw,id=blk0" -device virtio-blk-pci,drive=blk0,addr=0x4 \
	-kernel "$BUILD_DIR/zephyr/zephyr.elf" &
QEMU_PID=$!
trap 'kill "$QEMU_PID" 2>/dev/null' EXIT

screenshot_names() {
	tr -d '\r' <"$LOG" 2>/dev/null | sed -n 's/^SCREENSHOT \([A-Za-z0-9_-]*\)$/\1/p'
}

# ztest doesn't power QEMU off when it's done, so watch the console for its
# verdict (or a fatal error) instead of waiting for QEMU to exit -- serving
# screenshot requests as they show up.
shots_taken=0
SECONDS=0
while [ "$SECONDS" -lt "$TIMEOUT" ]; do
	while read -r name; do
		(echo "screendump $SHOTS_DIR/$name.png -f png"; sleep 0.5) | nc -U "$MON_SOCK" >/dev/null
		shots_taken=$((shots_taken + 1))
	done < <(screenshot_names | tail -n +$((shots_taken + 1)))

	grep -q -e 'PROJECT EXECUTION' -e 'ZEPHYR FATAL ERROR' "$LOG" 2>/dev/null && break
	kill -0 "$QEMU_PID" 2>/dev/null || break
	sleep 0.2
done

cat "$LOG"
grep -q 'PROJECT EXECUTION SUCCESSFUL' "$LOG" || exit 1

status=0
shots=()
while read -r name; do
	if [ ! -s "$SHOTS_DIR/$name.png" ]; then
		echo "FAIL: screenshot $name was not captured"
		status=1
	fi
	shots+=("$name")
done < <(screenshot_names)

for ((i = 0; i < ${#shots[@]}; i++)); do
	for ((j = i + 1; j < ${#shots[@]}; j++)); do
		if cmp -s "$SHOTS_DIR/${shots[i]}.png" "$SHOTS_DIR/${shots[j]}.png"; then
			echo "FAIL: screenshots ${shots[i]} and ${shots[j]} are identical"
			status=1
		fi
	done
done

if [ "${#shots[@]}" -gt 0 ] && [ "$status" -eq 0 ]; then
	echo "${#shots[@]} distinct screenshots in $SHOTS_DIR"
fi
exit "$status"
