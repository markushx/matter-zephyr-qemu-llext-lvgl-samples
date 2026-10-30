#!/usr/bin/env node
// Makes chip-tool a second admin of a node commissioned on a matterjs-server,
// so that scripts/send-llext.py can reach the ExtensionLoaderCluster. The server
// rejects commands on clusters it has no definition for and cannot be given one
// at runtime; this route leaves it untouched.
//
// Usage:
//   scripts/matterjs-share-node.mjs <ws-url> <server-node-id> <chip-tool-node-id> [chip-tool-path]
//
// Example:
//   scripts/matterjs-share-node.mjs ws://homeassistant.local:5580/ws 5 1
//   scripts/send-llext.py 5_nrf52840_thread/extensions/counter/build/counter.llext 1
import { spawnSync } from "node:child_process";
import { homedir } from "node:os";

const [url, serverNodeId, chipToolNodeId, chipTool = `${homedir()}/src/connectedhomeip-worktree-zephyr/out/chip-tool/chip-tool`] =
    process.argv.slice(2);
if (!url || !serverNodeId || !chipToolNodeId) {
    console.error("Usage: matterjs-share-node.mjs <ws-url> <server-node-id> <chip-tool-node-id> [chip-tool-path]");
    process.exit(1);
}

function openCommissioningWindow() {
    return new Promise((resolve, reject) => {
        const ws = new WebSocket(url);
        ws.onerror = () => reject(new Error(`cannot connect to ${url}`));
        ws.onmessage = event => {
            const message = JSON.parse(event.data);
            // The server greets every connection with its server info.
            if (message.sdk_version !== undefined) {
                console.log(`Server: ${message.sdk_version}`);
                ws.send(
                    JSON.stringify({
                        message_id: "share",
                        command: "open_commissioning_window",
                        args: { node_id: Number(serverNodeId), timeout: 300 },
                    }),
                );
            } else if (message.message_id === "share") {
                ws.close();
                if (message.error_code !== undefined) {
                    reject(new Error(`server error ${message.error_code}: ${message.details}`));
                } else {
                    resolve(message.result);
                }
            }
        };
    });
}

try {
    const { setup_manual_code: code } = await openCommissioningWindow();
    console.log(`Commissioning window open, pairing chip-tool as node ${chipToolNodeId}`);
    const result = spawnSync(chipTool, ["pairing", "code", chipToolNodeId, code], { stdio: "inherit" });
    process.exit(result.status ?? 1);
} catch (error) {
    console.error(error.message);
    process.exit(1);
}
