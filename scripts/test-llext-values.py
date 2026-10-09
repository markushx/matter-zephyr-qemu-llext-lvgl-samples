#!/usr/bin/env python3
"""End-to-end test for the ExtensionLoaderCluster's ExtensionValue attribute
(0xFFF1FC01 attribute 1, EP1): loads two different LLEXT extensions in turn
and checks that the attribute reads back each one's own get_value() result.

Needs a running, already-commissioned `llext/` sample (see README.md "LLEXT
loadable extensions") and both extensions built:
    llext/extensions/value42/build/value42.llext      -> 42
    llext/extensions/value1337/build/value1337.llext  -> 1337

value42 is loaded a second time at the end, so the test also passes on a
device that already had value1337 loaded and proves replacement works in
both directions.

Usage:
    scripts/test-llext-values.py <node-id> [chip-tool-path]
"""
import os
import re
import subprocess
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
EXTENSIONS_DIR = os.path.join(SCRIPT_DIR, "..", "llext", "extensions")
SEND_LLEXT = os.path.join(SCRIPT_DIR, "send-llext.py")

CLUSTER_ID = "0xFFF1FC01"
ATTR_TRANSFER_STATUS = "0"
ATTR_EXTENSION_VALUE = "1"
ENDPOINT = "1"
STATUS_LOADED = 2

DEFAULT_CHIP_TOOL = "/Users/markusbecker/src/connectedhomeip-worktree-zephyr/out/chip-tool/chip-tool"

# (extension name, expected ExtensionValue)
STEPS = [("value42", 42), ("value1337", 1337), ("value42", 42)]


def read_attribute(chip_tool, attribute_id, node_id):
    """Returns the attribute's integer value, or None if it reads as null."""
    args = [chip_tool, "any", "read-by-id", CLUSTER_ID, attribute_id, node_id, ENDPOINT]
    result = subprocess.run(args, capture_output=True, text=True)
    # chip-tool has no codegen for this cluster, so the value only shows up in
    # its pretty-printed report dump: "Data = 42 (signed)," / "Data = NULL".
    match = re.search(r"Data = (NULL|-?\d+)", result.stdout)
    if result.returncode != 0 or match is None:
        print(result.stdout)
        print(result.stderr, file=sys.stderr)
        raise RuntimeError(f"chip-tool read failed (attribute-id={attribute_id})")
    return None if match.group(1) == "NULL" else int(match.group(1))


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)

    node_id = sys.argv[1]
    chip_tool = sys.argv[2] if len(sys.argv) > 2 else DEFAULT_CHIP_TOOL

    failures = 0
    for name, expected in STEPS:
        llext_path = os.path.join(EXTENSIONS_DIR, name, "build", f"{name}.llext")
        send = subprocess.run([sys.executable, SEND_LLEXT, llext_path, node_id, chip_tool], capture_output=True, text=True)
        if send.returncode != 0:
            print(send.stdout)
            print(send.stderr, file=sys.stderr)
            raise RuntimeError(f"sending {llext_path} failed")

        status = read_attribute(chip_tool, ATTR_TRANSFER_STATUS, node_id)
        value = read_attribute(chip_tool, ATTR_EXTENSION_VALUE, node_id)
        ok = status == STATUS_LOADED and value == expected
        failures += not ok
        print(f"{'PASS' if ok else 'FAIL'}: {name}: TransferStatus={status} (want {STATUS_LOADED}), "
              f"ExtensionValue={value} (want {expected})")

    if failures:
        print(f"{failures} of {len(STEPS)} steps failed")
        sys.exit(1)
    print("All steps passed")


if __name__ == "__main__":
    main()
