import { app, BrowserWindow, ipcMain, dialog, shell } from "electron";
import * as path from "path";
import * as fs from "fs";
import { RustBridge } from "./rust-bridge";

let shellPath: string | undefined;

function ensureShellPath() {
  if (shellPath) return shellPath;
  const home = process.env.HOME || "";
  const candidates = [
    "/opt/homebrew/bin",
    "/usr/local/bin",
    "/usr/bin",
    "/bin",
    "/usr/sbin",
    "/sbin",
    `${home}/.cargo/bin`,
  ];
  const existing = new Set((process.env.PATH || "").split(":"));
  const additions = candidates.filter((c) => !existing.has(c));
  shellPath = [...additions, process.env.PATH].filter(Boolean).join(":");
  return shellPath;
}

let mainWindow: BrowserWindow | null = null;
let bridge: RustBridge;

function getMetalsharpDir(): string {
  return path.join(require("os").homedir(), ".metalsharp");
}

function isFirstLaunch(): boolean {
  const setupFile = path.join(getMetalsharpDir(), "setup.json");
  if (!fs.existsSync(setupFile)) return true;
  try {
    const cfg = JSON.parse(fs.readFileSync(setupFile, "utf8"));
    return !cfg.completed;
  } catch {
    return true;
  }
}

function ensureMetalsharpDirs() {
  const base = getMetalsharpDir();
  const dirs = ["games", "cache", "logs", "runtime/fna", "runtime/shims", "runtime/mono-x86", "prefix-gptk"];
  for (const d of dirs) {
    fs.mkdirSync(path.join(base, d), { recursive: true });
  }
}

async function createWindow() {
  mainWindow = new BrowserWindow({
    width: 1200,
    height: 800,
    minWidth: 900,
    minHeight: 600,
    title: "MetalSharp",
    backgroundColor: "#1a1118",
    titleBarStyle: "hiddenInset",
    trafficLightPosition: { x: 16, y: 16 },
    webPreferences: {
      preload: path.join(__dirname, "preload.js"),
      contextIsolation: true,
      nodeIntegration: false,
    },
  });

  mainWindow.loadFile(path.join(__dirname, "..", "renderer", "index.html"));
}

app.whenReady().then(async () => {
  ensureMetalsharpDirs();
  bridge = new RustBridge();
  await bridge.start();

  registerIpc();

  await createWindow();

  app.on("activate", () => {
    if (BrowserWindow.getAllWindows().length === 0) createWindow();
  });
});

app.on("window-all-closed", () => {
  bridge?.stop();
  if (process.platform !== "darwin") app.quit();
});

function registerIpc() {
  ipcMain.handle("backend:request", async (_e, method: string, url: string, body?: Record<string, unknown>) => {
    return bridge.request(method, url, body);
  });

  ipcMain.handle("app:is-first-launch", () => {
    return isFirstLaunch();
  });

  ipcMain.handle("app:eject-dmg", async () => {
    const exePath = app.getPath("exe");
    const dmgMatch = exePath.match(/\/Volumes\/([^/]+)/);
    if (dmgMatch) {
      const vol = dmgMatch[1];
      shell.openExternal("");
      const { execSync } = require("child_process");
      try {
        execSync(`hdiutil detach "/Volumes/${vol}" -quiet`);
        dialog.showMessageBox({
          type: "info",
          title: "MetalSharp",
          message: "MetalSharp has been ejected from the installer disk image.",
          buttons: ["OK"],
        });
      } catch {
        dialog.showMessageBox({
          type: "warning",
          title: "MetalSharp",
          message: "Could not eject the disk image. You can eject it manually from Finder.",
          buttons: ["OK"],
        });
      }
    } else {
      dialog.showMessageBox({
        type: "info",
        title: "MetalSharp",
        message: "MetalSharp is not running from a disk image.",
        buttons: ["OK"],
      });
    }
  });

  ipcMain.handle("app:install-deps", async (_e, command: string) => {
    return new Promise((resolve) => {
      const { spawn } = require("child_process");
      const env = { ...process.env, PATH: ensureShellPath() };
      const brewPath = ["/opt/homebrew/bin/brew", "/usr/local/bin/brew"].find((p) => {
        try { fs.accessSync(p); return true; } catch { return false; }
      });

      let proc;
      if (command.startsWith("brew ")) {
        if (!brewPath) {
          resolve({ ok: false, error: "Homebrew is not installed. Install it from https://brew.sh first." });
          return;
        }
        const args = command.split(/\s+/).slice(1);
        proc = spawn(brewPath, args, { env });
      } else if (command.startsWith("script:")) {
        const scriptName = command.slice(7);
        const scriptPath = path.join(path.dirname(app.getPath("exe")), "..", "scripts", scriptName);
        const fallbackPath = path.join(getMetalsharpDir(), "scripts", scriptName);
        const resolved = fs.existsSync(scriptPath) ? scriptPath : fs.existsSync(fallbackPath) ? fallbackPath : null;
        if (!resolved) {
          resolve({ ok: false, error: `Script not found: ${scriptName}` });
          return;
        }
        proc = spawn("/bin/bash", [resolved], { env });
      } else {
        const parts = command.split(/\s+/);
        proc = spawn(parts[0], parts.slice(1), { env });
      }

      let stdout = "";
      let stderr = "";
      let settled = false;

      const timer = setTimeout(() => {
        if (!settled) {
          settled = true;
          proc.kill();
          resolve({ ok: false, error: "Installation timed out after 10 minutes. Try running the command manually in Terminal." });
        }
      }, 10 * 60 * 1000);

      proc.stdout.on("data", (d: Buffer) => (stdout += d.toString()));
      proc.stderr.on("data", (d: Buffer) => (stderr += d.toString()));
      proc.on("error", (err: Error) => {
        if (!settled) { settled = true; clearTimeout(timer); }
        resolve({ ok: false, error: `Failed to run "${command}": ${err.message}` });
      });
      proc.on("close", (code: number) => {
        if (settled) return;
        settled = true;
        clearTimeout(timer);
        const combined = `${stdout}${stderr}`;
        const ok = code === 0 || combined.includes("already installed");
        resolve({ ok, error: ok ? null : combined.split("\n").filter(Boolean).pop() ?? `exit code ${code}` });
      });
    });
  });
}
