#!/usr/bin/env python3
"""Send a Zephyr LLEXT (.llext) binary to a commissioned device over Matter,
via the manufacturer-specific ExtensionLoaderCluster (0xFFF1FC01, EP1) --
see llext/src/llext/ExtensionLoaderCluster.{h,cpp} and plan/plan-detailed.md
Milestone 3.

Drives chip-tool's generic `any command-by-id` invocation (no ZAP/codegen
entry needed for this cluster) once per LoadStart/LoadChunk/LoadDone.

Usage:
    scripts/send-llext.py <path-to.llext> <node-id> [chip-tool-path]
"""
import binascii
import subprocess
import sys
import zlib

CLUSTER_ID = "0xFFF1FC01"
CMD_LOAD_START = "0"
CMD_LOAD_CHUNK = "1"
CMD_LOAD_DONE = "2"
ENDPOINT = "1"
CHUNK_SIZE = 900

DEFAULT_CHIP_TOOL = "/Users/markusbecker/src/connectedhomeip-worktree-zephyr/out/chip-tool/chip-tool"


def run_command(chip_tool, command_id, payload, node_id):
    args = [chip_tool, "any", "command-by-id", CLUSTER_ID, command_id, payload, node_id, ENDPOINT]
    print(f"+ {' '.join(args)}")
    result = subprocess.run(args, capture_output=True, text=True)
    if "Run command failure" in result.stdout or result.returncode != 0:
        print(result.stdout)
        print(result.stderr, file=sys.stderr)
        raise RuntimeError(f"chip-tool command failed (command-id={command_id})")


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)

    llext_path = sys.argv[1]
    node_id = sys.argv[2]
    chip_tool = sys.argv[3] if len(sys.argv) > 3 else DEFAULT_CHIP_TOOL

    with open(llext_path, "rb") as f:
        data = f.read()

    total_size = len(data)
    crc32 = zlib.crc32(data) & 0xFFFFFFFF
    print(f"Loading {llext_path}: {total_size} bytes, crc32=0x{crc32:08x}")

    run_command(chip_tool, CMD_LOAD_START, f'{{"0": {total_size}, "1": {crc32}}}', node_id)

    for offset in range(0, total_size, CHUNK_SIZE):
        chunk = data[offset : offset + CHUNK_SIZE]
        hex_str = binascii.hexlify(chunk).decode("ascii")
        payload = f'{{"0": {offset}, "1": "hex:{hex_str}"}}'
        run_command(chip_tool, CMD_LOAD_CHUNK, payload, node_id)

    run_command(chip_tool, CMD_LOAD_DONE, "{}", node_id)
    print("Done. Check the device console for load/run confirmation, or read")
    print(f"  Status: chip-tool any read-by-id {CLUSTER_ID} 0 {node_id} {ENDPOINT}")


if __name__ == "__main__":
    main()
