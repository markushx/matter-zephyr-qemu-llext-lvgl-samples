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
# whichever slot is next after e1000, and on q35 that slot's legacy INTx pin
# computes to the SAME GSI (11) as e1000 -- confirmed via QEMU monitor
# `info pci`. Slot 4 computes to GSI 10, matching the interrupts property in
# persistent.overlay's virtio-blk-pci node. Without this, the mismatch
# causes fs_mount() to hang forever (the real completion interrupt fires on
# a vector Zephyr's static IDT never wired up).
DISK_ARGS=(-drive "file=$DISK_IMAGE,if=none,format=raw,id=blk0" -device virtio-blk-pci,drive=blk0,addr=0x4)

exec sudo "$QEMU_BIN" \
	-m 32 -cpu qemu32,+nx,+pae -machine q35 \
	-device isa-debug-exit,iobase=0xf4,iosize=0x04 -machine acpi=off \
	-pidfile "$BUILD_DIR/qemu.pid" \
	-chardev stdio,id=con,mux=on -serial chardev:con -mon chardev=con,mode=readline \
	-nographic -no-reboot \
	-netdev vmnet-shared,id=n1 -device e1000,netdev=n1 \
	"${DISK_ARGS[@]}" \
	-kernel "$BUILD_DIR/zephyr/zephyr.elf"
