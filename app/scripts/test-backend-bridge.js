#!/usr/bin/env node
const assert = require("node:assert/strict");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const net = require("node:net");

function freePort() {
  return new Promise((resolve, reject) => {
    const server = net.createServer();
    server.once("error", reject);
    server.listen(0, "127.0.0.1", () => {
      const address = server.address();
      server.close(() => resolve(address.port));
    });
  });
}

async function main() {
  const home = fs.mkdtempSync(path.join(os.tmpdir(), "metalsharp-bridge-"));
  process.env.METALSHARP_HOME = home;
  process.env.METALSHARP_DEV = "1";
  process.env.METALSHARP_PORT = String(await freePort());

  const { BackendBridge } = require(path.join(__dirname, "..", "dist", "main", "backend-bridge.js"));
  const bridge = new BackendBridge({ devMode: true, metalsharpHome: home });
  try {
    const started = await bridge.start();
    assert.equal(started.ok, true, started.error);
    assert.equal(await bridge.isAlive(), true);

    const status = await bridge.request("GET", "/status");
    assert.equal(status.ok, true);
    assert.equal(status.version, "0.56.1");
    assert.equal(status.metalsharp_home, home);
    assert.equal(typeof status.pid, "number");
    const firstPid = status.pid;

    const setup = await bridge.request("GET", "/setup/state");
    assert.equal(setup.ok, true);
    assert.equal(typeof setup.completed, "boolean");

    const restarted = await bridge.restart();
    assert.equal(restarted.ok, true, restarted.error);
    const restartedStatus = await bridge.request("GET", "/status");
    assert.equal(restartedStatus.ok, true);
    assert.notEqual(restartedStatus.pid, firstPid);

    await bridge.stop();
    assert.equal(await bridge.isAlive(), false);
    console.log("Electron BackendBridge lifecycle passed against hand-written metalsharp-backend.");
  } finally {
    await bridge.stop();
    fs.rmSync(home, { recursive: true, force: true });
  }
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
