"use strict";
// Compatibility entry point: the former static server now starts the game backend.
const { spawn } = require("node:child_process");
const path = require("node:path");
const child = spawn(process.env.PYTHON || "python",
  [path.join(__dirname, "server", "game_server.py")], { stdio: "inherit" });
child.on("error", error => {
  console.error("Cannot start the game server. Install Python 3.11+ or set PYTHON:", error.message);
  process.exitCode = 1;
});
child.on("exit", code => { process.exitCode = code || 0; });
for (const signal of ["SIGINT", "SIGTERM"]) {
  process.on(signal, () => child.kill(signal));
}
