# Matter Light Bulb on Zephyr + QEMU

A commissionable Matter (project-chip/connectedhomeip) light bulb running on
Zephyr RTOS in QEMU (`qemu_x86`), commissioned over real Ethernet (no
BLE/Thread/Wi-Fi). See `plan/plan.md` and `plan/plan-detailed.md` for the
full design, milestones, and a retrospective of every real bug found getting
here (crypto backend, GN toolchain, SYS_INIT ordering, PCI IRQ routing,
etc.) — worth reading before touching the build if something breaks.

## Samples

Three independent, self-contained Zephyr applications, each buildable on its
own with no extra CLI flags:

- **[`plain/`](plain/)** — the base light bulb: on/off + level control,
  commissioning over Ethernet, persistent storage (survives QEMU restarts).
  Start here.
- **[`lvgl/`](lvgl/)** — `plain/` plus an LVGL widget (a yellow/grey arc +
  ON/OFF label) that reflects the bulb's on/off + level state in a QEMU
  window instead of only the console log.
- **[`llext/`](llext/)** — `plain/` plus a manufacturer-specific Matter
  cluster that can receive a Zephyr LLEXT (Linkable Loadable Extension)
  binary over Matter and load + run it at runtime. Includes a sample
  extension (`llext/extensions/hello/`) and a host-side script
  (`scripts/send-llext.py`) to send it.

Each sample carries its own `prj.conf`, `boards/qemu_x86.{conf,overlay}`,
`include/CHIPProjectConfig.h`, and `src/`. `boards/qemu_x86.overlay` is
Zephyr's auto-detected board-devicetree-overlay convention — it's picked up
automatically by `west build`, no `-DEXTRA_DTC_OVERLAY_FILE` needed.

## Prerequisites

- Zephyr workspace at `~/src/zephyrproject` (west installed, `qemu_x86`
  board support present).
- A `connectedhomeip` checkout/worktree at
  `~/src/connectedhomeip-worktree-zephyr`, bootstrapped (its `.environment/`
  present). Each sample's `CMakeLists.txt` defaults `CHIP_ROOT` to this
  path; override with `-DCHIP_ROOT=...` if yours lives elsewhere.
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
  ~/src/matter-zephyr-qemu/plain -d ~/src/matter-zephyr-qemu/build-plain
```

Swap `plain` for `lvgl` or `llext` (and `build-plain` for `build-lvgl` /
`build-llext`) to build one of the other samples — each needs its own build
directory since their Kconfig differs, but the command is otherwise
identical; no extra `-DEXTRA_CONF_FILE`/`-DEXTRA_DTC_OVERLAY_FILE` flags
required.

(`activate.sh` prints a harmless `ERR could not find arm-none-eabi-gcc`
check failure — this project doesn't need that toolchain; ignore it. Use
`west`'s absolute path, not a bare `west` on `PATH`: CHIP's own bootstrap
venv also ships a `west` package on an older Python, and if it shadows
Zephyr's own `west` the build fails with a `pathlib` crash. `west build`
must also be run from within the `~/src/zephyrproject` workspace — cd there
first, or pass the sample directory as an absolute path from wherever you
run it.)

## Run

```
scripts/run-qemu-macos.sh [build-plain|build-lvgl|build-llext]
```

Defaults to `build-plain`. Requires `sudo` (vmnet needs root) — run it
yourself in an interactive terminal so you can enter your password; it
can't be driven non-interactively. On first run it creates a 16 MiB disk
image at `<BUILD_DIR>/persistent.img` and Zephyr formats it FAT on first
boot. `run-qemu-macos.sh` auto-detects `CONFIG_DISPLAY=y` in the build
directory's `.config` (i.e. the `lvgl/` sample) and opens a QEMU window
(`-device ramfb -vga none -display cocoa`) instead of running headless;
serial/monitor still work as normal over the terminal.

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
`<BUILD_DIR>/persistent.img`, instead of Zephyr's default `flash_simulator`
(which is RAM-backed and loses everything on every QEMU restart). This means
a device commissioned in one run is still commissioned after killing and
restarting QEMU, as long as the same disk image is reused — no
`isa-debug-exit`/normal QEMU shutdown, kill, or crash touches the image file
on disk. All three samples build with this enabled.

**To reset persistence** (force re-commissioning, e.g. after changing the
data model or to start clean): delete the disk image and let the script
recreate it on the next run.

```
rm build-plain/persistent.img
scripts/run-qemu-macos.sh
```

## LVGL bulb UI (`lvgl/`)

An LVGL widget (a yellow/grey arc + ON/OFF label, `lvgl/src/gui/BulbUi.cpp`)
reflects the Matter on/off + level state in a QEMU window, instead of only
the console log.

`lvgl/boards/qemu_x86.overlay` shrinks the declared `dram0`/`zephyr,sram`
size to exclude the top 4 MiB the board reserves for the ramfb framebuffer
-- without it, the linker is free to place ordinary kernel/network data in
the same physical bytes the framebuffer driver claims exclusively, which
doesn't itself break networking but is a real memory-safety bug in the
board's own devicetree. The actual networking breakage this port hit was
separate: `-vga none` (needed whenever `CONFIG_DISPLAY=y`, since ramfb
replaces the default VGA adapter) removes the PCI device normally at slot
1, which shifts `e1000`'s auto-assigned slot and, with it, its real
interrupt line -- silently breaking all incoming UDP delivery (confirmed
via GDB: the e1000 ISR simply never fired). `run-qemu-macos.sh` pins both
`e1000` and `virtio-blk-pci` to explicit PCI slots to make this stable
regardless of what else is on the command line -- see its comments for the
full story.

## LLEXT loadable extensions (`llext/`)

A manufacturer-specific Matter cluster (`0xFFF1FC01`, EP1,
`llext/src/llext/ExtensionLoaderCluster.cpp`) receives a Zephyr LLEXT binary
over Matter in chunks, verifies it, and loads + runs it at runtime -- see
`plan/plan-detailed.md`'s Milestone 3 section for the full design and the
real bugs found getting it working (a QEMU CPU-model/CLFLUSH mismatch was
the big one).

### Building the sample extension

`llext/extensions/hello/` is a trivial standalone extension (prints a
message, proving the load-then-call round trip) built with Zephyr's LLEXT
EDK, not as part of the main app build:

```
cd build-llext
cmake --build . --target llext-edk   # produces zephyr/llext-edk.tar.xz
mkdir -p /tmp/edk && tar -xf zephyr/llext-edk.tar.xz -C /tmp/edk
cd ../llext/extensions/hello
export ZEPHYR_SDK_INSTALL_DIR=~/zephyr-sdk-1.0.1
export PATH="$ZEPHYR_SDK_INSTALL_DIR/gnu/x86_64-zephyr-elf/bin:$PATH"
cmake -B build -DLLEXT_EDK_INSTALL_DIR=/tmp/edk/llext-edk
cmake --build build   # produces build/hello.llext
```

(`cmake --build build-llext --target llext-edk` needs the same CHIP
bootstrap environment sourced as a normal build -- see "Build" above --
plus, if invoked standalone rather than via `west build`, `PATH` needs
`~/src/zephyrproject/zephyr/.venv/bin` first so its own internal Python
lookup finds the venv, not a bare system `python3`.)

### Sending the extension

```
scripts/send-llext.py llext/extensions/hello/build/hello.llext <node-id>
```

Drives chip-tool's generic `any command-by-id` invocation (no ZAP/codegen
entry needed for a manufacturer cluster) to send `LoadStart`/`LoadChunk`×N/
`LoadDone`. Watch the device's own console (or `<BUILD_DIR>/console.log`) for
`hello from llext` -- confirms the extension was loaded *and executed*, not
just linked. Read back progress with:

```
out/chip-tool/chip-tool any read-by-id 0xFFF1FC01 0 <node-id> 1   # Status: 0=Idle 1=Receiving 2=Loaded 3=Error
```

## Debugging tools

`run-qemu-macos.sh` also supports:
- `GDB=1 scripts/run-qemu-macos.sh ...` -- exposes a GDB stub on `tcp::1234`
  (`-s`, boot proceeds normally). Attach with e.g.
  `~/zephyr-sdk-*/gnu/x86_64-zephyr-elf/bin/x86_64-zephyr-elf-gdb <build>/zephyr/zephyr.elf -ex "target remote :1234"`.
- A second, scriptable QEMU monitor at `<BUILD_DIR>/mon.sock` (HMP, in
  addition to the normal interactive one on the terminal via `Ctrl-A C`) --
  useful for e.g. `info pci` to check real PCI slot/IRQ assignments without
  needing to switch away from the serial console.
- Console output (serial + the interactive monitor) is duplicated to
  `<BUILD_DIR>/console.log` (truncated each run) for inspection without
  copy-pasting from the terminal.

## Logging

Each sample's `include/CHIPProjectConfig.h` mutes Progress/Detail-level
logging from CHIP's Exchange Manager (`EM`), Secure Channel (`SC`), and
interaction-model/data-management (`DMG`) log modules — these are extremely
chatty (every message send/receive, every attribute read) and were drowning
out everything else, including ZCL (`ZCL`) logging, in the 16 KB log ring
buffer. Error-level logging from all three is left intact; ZCL and all
other modules are unaffected.
