#!/usr/bin/env bash
# Run the built qemu_x86 image with real host networking (vmnet) instead of
# Zephyr's default tap netdev, which requires a pre-existing Linux tap
# interface and doesn't work out of the box on macOS.
#
# vmnet requires root, hence the sudo. Requires QEMU built with vmnet support
# (Homebrew's qemu formula has it).
#
# The app is always built with persistent.conf/persistent.overlay (see
# app/), so this always attaches a virtio-blk disk backed by a real host
# file -- CHIP's Settings storage (fabrics, ACLs, session resumption, etc.)
# survives QEMU restarts. The image is created (16 MiB, sparse) on first
# use; Zephyr formats it FAT itself on first boot. To reset commissioning
# state, delete the disk image (see README.md).
#
# If the build was also configured with app/lvgl.conf (CONFIG_DISPLAY=y in
# the build's .config), this opens a QEMU window (ramfb device, mirroring
# Zephyr's own cmake/emu/qemu/display.cmake) showing the LVGL bulb UI
# instead of running headless.
#
# Console output (serial + monitor, exactly what appears in the terminal)
# is also duplicated to $BUILD_DIR/console.log (truncated each run), so it
# can be inspected without copy-pasting from the terminal.
#
# Set GDB=1 to also expose a GDB stub on tcp::1234 (-s), for e.g.
# `x86_64-zephyr-elf-gdb $BUILD_DIR/zephyr/zephyr.elf -ex "target remote :1234"`.
# Boot proceeds normally (no -S); attach whenever you like.
#
# Usage: run-qemu-macos.sh [BUILD_DIR] [DISK_IMAGE]
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${1:-$REPO_ROOT/build-persistent}"
DISK_IMAGE="${2:-$BUILD_DIR/persistent.img}"

QEMU_BIN="$(brew --prefix qemu)/bin/qemu-system-i386"

if [ ! -f "$DISK_IMAGE" ]; then
	echo "Creating persistent disk image: $DISK_IMAGE"
	truncate -s 16M "$DISK_IMAGE"
fi

# addr=0x4 pins the PCI slot: without it QEMU auto-assigns virtio-blk-pci to
# whichever slot is next available, and on q35 that slot's legacy INTx pin
# can land on the SAME GSI as another device -- confirmed via QEMU monitor
# `info pci`. Slot 4 computes to GSI 10, matching the interrupts property in
# persistent.overlay's virtio-blk-pci node. Without this, the mismatch
# causes fs_mount() to hang forever (the real completion interrupt fires on
# a vector Zephyr's static IDT never wired up).
DISK_ARGS=(-drive "file=$DISK_IMAGE,if=none,format=raw,id=blk0" -device virtio-blk-pci,drive=blk0,addr=0x4)

# e1000 must ALSO be pinned (addr=0x2, which computes to GSI 11, matching
# e1000's `interrupts` property in the board's qemu_x86.dts): without an
# explicit slot, e1000's position depends on what else occupies earlier PCI
# slots. Normally the default VGA adapter sits at slot 1, pushing e1000 to
# slot 2/3 (GSI 11) -- matching the devicetree. But `-vga none` (below, used
# whenever CONFIG_DISPLAY=y) removes that VGA device, so e1000 shifts down to
# slot 1 (GSI 10) instead -- colliding with virtio-blk-pci's GSI 10 above,
# and no longer matching what the static IDT was built for. Confirmed via
# `info pci`: e1000 silently never fires its ISR in that state (zero
# packets ever reach it, mDNS multicast still worked, masking this for a
# while). Pinning removes the dependency on VGA's presence entirely.
NET_ARGS=(-netdev vmnet-shared,id=n1 -device e1000,netdev=n1,addr=0x2)

DISPLAY_ARGS=(-nographic)
if grep -q '^CONFIG_DISPLAY=y' "$BUILD_DIR/zephyr/.config" 2>/dev/null; then
	DISPLAY_ARGS=(-device ramfb -vga none -display cocoa)
fi

GDB_ARGS=()
if [ "${GDB:-0}" = "1" ]; then
	GDB_ARGS=(-s)
fi

exec sudo "$QEMU_BIN" \
	-m 32 -cpu qemu32,+nx,+pae -machine q35 \
	-device isa-debug-exit,iobase=0xf4,iosize=0x04 -machine acpi=off \
	-pidfile "$BUILD_DIR/qemu.pid" \
	-chardev "stdio,id=con,mux=on,logfile=$BUILD_DIR/console.log" -serial chardev:con -mon chardev=con,mode=readline \
	-chardev "socket,id=mon2,path=$BUILD_DIR/mon.sock,server=on,wait=off" -mon chardev=mon2,mode=readline \
	-no-reboot \
	"${DISPLAY_ARGS[@]}" \
	${GDB_ARGS[@]+"${GDB_ARGS[@]}"} \
	"${NET_ARGS[@]}" \
	"${DISK_ARGS[@]}" \
	-kernel "$BUILD_DIR/zephyr/zephyr.elf"
