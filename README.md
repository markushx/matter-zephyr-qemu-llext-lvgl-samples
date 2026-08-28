# Matter Light Bulb on Zephyr + QEMU

A commissionable Matter (project-chip/connectedhomeip) light bulb running on
Zephyr RTOS in QEMU (`qemu_x86`), commissioned over real Ethernet (no
BLE/Thread/Wi-Fi). See `plan/plan.md` and `plan/plan-detailed.md` for the
full design, milestones, and a retrospective of every real bug found getting
here (crypto backend, GN toolchain, SYS_INIT ordering, PCI IRQ routing,
etc.) — worth reading before touching the build if something breaks.

## Layout

- `app/` — the Zephyr application (CMakeLists.txt, `prj.conf`,
  `persistent.conf`/`persistent.overlay`, sources, `CHIPProjectConfig.h`).
- `scripts/run-qemu-macos.sh` — boots the built image with real host
  networking (vmnet) and persistent storage (virtio-blk disk).
- `build-persistent/` — the (gitignored) build output. This is the only
  build directory; the app is always built with persistent storage enabled.
- `plan/` — design docs and the M1 retrospective.

## Prerequisites

- Zephyr workspace at `~/src/zephyrproject` (west installed, `qemu_x86`
  board support present).
- A `connectedhomeip` checkout/worktree at
  `~/src/connectedhomeip-worktree-zephyr`, bootstrapped (its `.environment/`
  present). `app/CMakeLists.txt` defaults `CHIP_ROOT` to this path; override
  with `-DCHIP_ROOT=...` if yours lives elsewhere.
- `zap-cli` on `PATH` for data-model codegen (see
  `.environment/extra-cipd/zap` under the CHIP checkout if you followed the
  same bootstrap as this project).
- QEMU with vmnet support (Homebrew's `qemu` formula has it).
- `chip-tool` built from the same CHIP checkout
  (`out/chip-tool/chip-tool`) for commissioning/control from the host.

## Build

```
source ~/src/connectedhomeip-worktree-zephyr/.environment/activate.sh
export PATH="~/src/connectedhomeip-worktree-zephyr/.environment/extra-cipd/zap:$PATH"

~/src/zephyrproject/zephyr/.venv/bin/west build -b qemu_x86 \
  ~/src/matter-zephyr-qemu/app \
  -d ~/src/matter-zephyr-qemu/build-persistent -- \
  -DEXTRA_CONF_FILE=persistent.conf -DEXTRA_DTC_OVERLAY_FILE=persistent.overlay
```

(`activate.sh` prints a harmless `ERR could not find arm-none-eabi-gcc`
check failure — this project doesn't need that toolchain; ignore it. Use
`west`'s absolute path, not a bare `west` on `PATH`: CHIP's own bootstrap
venv also ships a `west` package on an older Python, and if it shadows
Zephyr's own `west` the build fails with a `pathlib` crash.)

## Run

```
scripts/run-qemu-macos.sh
```

Requires `sudo` (vmnet needs root) — run it yourself in an interactive
terminal so you can enter your password; it can't be driven non-interactively.
On first run it creates a 16 MiB disk image at
`build-persistent/persistent.img` and Zephyr formats it FAT on first boot.

Optional args: `scripts/run-qemu-macos.sh [BUILD_DIR] [DISK_IMAGE]`.

### Commissioning

With the QEMU console running, from another terminal on the host:

```
dns-sd -B _matterc._udp                              # confirm the device is discoverable
out/chip-tool/chip-tool pairing onnetwork 1 20202021  # PASE + CASE
out/chip-tool/chip-tool onoff toggle 1 1
out/chip-tool/chip-tool levelcontrol move-to-level 128 0 0 0 1 1
```

(paths relative to the `connectedhomeip` checkout's `chip-tool` build output)

## Persistent storage

CHIP's Settings (fabric table, ACLs, session resumption, etc.) are stored in
a file on a FAT volume on a virtio-blk disk backed by
`build-persistent/persistent.img`, instead of Zephyr's default `flash_simulator`
(which is RAM-backed and loses everything on every QEMU restart). This means
a device commissioned in one run is still commissioned after killing and
restarting QEMU, as long as the same disk image is reused — no
`isa-debug-exit`/normal QEMU shutdown, kill, or crash touches the image file
on disk.

**To reset persistence** (force re-commissioning, e.g. after changing the
data model or to start clean): delete the disk image and let the script
recreate it on the next run.

```
rm build-persistent/persistent.img
scripts/run-qemu-macos.sh
```

## LVGL bulb UI (optional)

An opt-in LVGL widget (a yellow/grey arc + ON/OFF label, `app/src/gui/BulbUi.cpp`)
reflects the Matter on/off + level state in a QEMU window, instead of only the
console log. It's off by default -- enable it with an extra Kconfig fragment
(`app/lvgl.conf`), which needs its own build directory since it changes the
`.config`:

```
~/src/zephyrproject/zephyr/.venv/bin/west build -b qemu_x86 \
  app -d build-lvgl -- \
  -DEXTRA_CONF_FILE="persistent.conf;lvgl.conf" -DEXTRA_DTC_OVERLAY_FILE=persistent.overlay

scripts/run-qemu-macos.sh build-lvgl
```

`run-qemu-macos.sh` auto-detects `CONFIG_DISPLAY=y` in the build directory's
`.config` and opens a QEMU window (`-device ramfb -vga none -display cocoa`)
instead of running headless; serial/monitor still work as normal over the
terminal. No devicetree overlay is needed for the display itself --
`qemu_x86`'s board `.dts` already has the `ramfb0` node enabled
unconditionally.

## Logging

`app/include/CHIPProjectConfig.h` mutes Progress/Detail-level logging from
CHIP's Exchange Manager (`EM`), Secure Channel (`SC`), and interaction-model/
data-management (`DMG`) log modules — these are extremely chatty (every
message send/receive, every attribute read) and were drowning out
everything else, including ZCL (`ZCL`) logging, in the 16 KB log ring
buffer. Error-level logging from all three is left intact; ZCL and all
other modules are unaffected.
