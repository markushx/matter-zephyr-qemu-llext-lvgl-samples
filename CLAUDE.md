# CLAUDE.md

Guidance for Claude Code working in this repo. Read `plan/plan.md` and
`plan/plan-detailed.md` first — they hold the actual design, milestone
status, and a retrospective of every real bug found so far. This file is
about *how to work here*, not what the project is.

## Do not touch

- `~/src/connectedhomeip` — the user's own checkout, on a corporate branch
  (`v1.0-tridonic`) with in-progress uncommitted work. Never `git checkout`,
  `reset`, or otherwise modify it. This project uses a separate `git
  worktree` at `~/src/connectedhomeip-worktree-zephyr` (same repo, updated
  `origin/master`) instead — reuse that one.
- `~/src/zephyrproject` — the user's existing Zephyr workspace, reused
  as-is. Don't re-checkout or `west update` it without asking.
- `build-persistent/persistent.img` — this is the "commissioned device's
  flash". Deleting it resets commissioning state (documented in README.md);
  don't delete it incidentally while cleaning up build artifacts.

## Environment gotchas (each cost real debugging time — don't relitigate)

- **Always invoke `west` via its absolute path**:
  `~/src/zephyrproject/zephyr/.venv/bin/west`. CHIP's own bootstrap venv
  also ships a `west` package on an older Python; if it shadows Zephyr's own
  `west` on `PATH`, the build fails with a `pathlib.relative_to` crash.
- Source `~/src/connectedhomeip-worktree-zephyr/.environment/activate.sh`
  before building. It prints a harmless `ERR could not find
  arm-none-eabi-gcc` check failure — this project doesn't need that
  toolchain (Ethernet-only, no other SoC targets) — ignore it, don't try to
  "fix" it by installing the ARM toolchain.
- `zap-cli` must be on `PATH` for data-model codegen:
  `~/src/connectedhomeip-worktree-zephyr/.environment/extra-cipd/zap`.
- Never point a second `cipd ensure` at the same root the bootstrap already
  uses — it wipes previously-installed packages (`cpython3`, `gn`, `ninja`).
  Use a separate root for anything installed outside the bootstrap's own
  flow (that's why `extra-cipd/zap` is a distinct directory).
- There is only one build directory, `build-persistent/`, and the app is
  always built with `persistent.conf`/`persistent.overlay`
  (`-DEXTRA_CONF_FILE=persistent.conf -DEXTRA_DTC_OVERLAY_FILE=persistent.overlay`).
  Don't reintroduce a separate non-persistent build — the persistent config
  is a strict superset and `run-qemu-macos.sh` assumes it.

## Tests you can run yourself

- `scripts/run-llext-ztest.sh` builds and runs the `ExtensionLoaderCluster`
  ztest suite (`llext/tests/extension_value/`) under QEMU with user-mode
  networking -- no sudo, so unlike `run-qemu-macos.sh` it works from Claude's
  Bash tool. Run it after touching `llext/src/llext/` or `llext/extensions/`.
- `scripts/run-llext-ztest.sh llext-lvgl/tests/extension_ui` does the same
  for the `llext-lvgl/` sample and saves a QEMU screendump per test step to
  `build-llext-lvgl-test/screenshots/*.png` -- Read those to actually see
  what LVGL rendered; the script itself only checks they all differ. Run it
  after touching `llext-lvgl/src/` or `llext-lvgl/extensions/`.
  Anything that needs real Matter traffic (commissioning, chip-tool) still
  needs the user to run `run-qemu-macos.sh`.

## Boot/QEMU debugging

- `run-qemu-macos.sh` needs `sudo` (vmnet requires root) and **cannot be run
  non-interactively** — Claude's Bash tool has no way to supply a password.
  Ask the user to run it themselves in their own terminal and paste back
  the console output, the same way earlier M1/persistence verification was
  done. Don't attempt `sudo -n` workarounds or ask the user for their
  password.
- For silent early-boot crashes (no console output at all): add
  `-d int,guest_errors -D <file>` to the QEMU invocation to catch the raw
  CPU exception trace, then `qemu ... -s -S` + a batch GDB session
  (`x86_64-zephyr-elf-gdb -batch -x <cmds> build-persistent/zephyr/zephyr.elf`,
  breaking on `z_x86_do_kernel_oops`/`abort`, printing `*esf`,
  `addr2line`/disassembling the resolved function) gets a real backtrace
  through Zephyr's exception trampolines — much faster than bisecting
  Kconfig options.
- For silent *hangs* (boot log just stops, no crash, no error): suspect a
  PCI IRQ mismatch before anything else on this board. `qemu_x86`'s x86 IDT
  is generated *statically at build time* from the devicetree's declared
  `interrupts` property, but QEMU's actual PCI slot→GSI routing is
  positional (depends on `-device` order/`addr=` on the command line). If a
  device's real GSI doesn't match what its devicetree node declares, its
  completion interrupts silently vanish and whatever's waiting on them
  blocks forever with nothing to log. Verify ground truth with QEMU's HMP
  monitor: add `-monitor unix:/tmp/mon.sock,server,nowait` and run
  `info pci` over it (see `plan-detailed.md`'s "Persistent storage" section
  for a worked example) rather than guessing from devicetree alone.

## Conventions

- Opt-in features get their own `EXTRA_CONF_FILE`/`EXTRA_DTC_OVERLAY_FILE`
  pair (e.g. `persistent.conf`/`persistent.overlay`), not edits to the
  default `prj.conf`/board overlay — additive, so the base config still
  builds standalone.
- Local patches to the `connectedhomeip-worktree-zephyr` worktree (a
  separate repo) are tracked in `plan-detailed.md`'s retrospective, not
  committed there — that worktree is scratch space, not this project's
  source of truth. If you make a new patch there, document it in
  `plan-detailed.md` the same way.
- Don't commit `build-persistent/` or `.cache/` (gitignored) — build
  artifacts, hundreds of MB, fully reproducible from source.
- Update `plan/plan-detailed.md` when you find and fix a real bug (not just
  a config tweak) — that log is what keeps this port maintainable if it's
  revisited or ported to another board.
