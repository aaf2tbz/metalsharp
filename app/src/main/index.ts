import { app, BrowserWindow, dialog, ipcMain, shell } from "electron";
import * as fs from "fs";
import * as path from "path";
import { RustBridge } from "./rust-bridge";
import { UpdaterBridge } from "./updater-bridge";

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
let updaterBridge: UpdaterBridge;
let steamappsWatcher: fs.FSWatcher | null = null;

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
  const dirs = ["games", "cache", "logs", "runtime/fna", "runtime/shims", "runtime/mono-x86", "runtime/dxvk-1.10.3"];
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
    icon: path.join(__dirname, "..", "..", "build", "icon.png"),
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

function startSteamappsWatcher() {
  const steamappsDir = path.join(
    getMetalsharpDir(),
    "prefix-steam",
    "drive_c",
    "Program Files (x86)",
    "Steam",
    "steamapps",
  );

  if (!fs.existsSync(steamappsDir)) return;

  let debounceTimer: ReturnType<typeof setTimeout> | null = null;

  try {
    steamappsWatcher = fs.watch(steamappsDir, (eventType, filename) => {
      if (!filename) return;
      if (filename.startsWith("appmanifest_") && filename.endsWith(".acf")) {
        if (debounceTimer) clearTimeout(debounceTimer);
        debounceTimer = setTimeout(() => {
          if (mainWindow && !mainWindow.isDestroyed()) {
            mainWindow.webContents.send("steamapps:changed");
          }
        }, 2000);
      }
    });
  } catch {
    // steamapps dir may not exist yet
  }
}

function cleanup() {
  if (steamappsWatcher) {
    steamappsWatcher.close();
    steamappsWatcher = null;
  }
  bridge?.stop();
}

app.whenReady().then(async () => {
  ensureMetalsharpDirs();
  bridge = new RustBridge();
  updaterBridge = new UpdaterBridge(bridge.getPort());
  await bridge.start();

  registerIpc();

  await createWindow();
  startSteamappsWatcher();

  app.on("activate", () => {
    if (BrowserWindow.getAllWindows().length === 0) createWindow();
  });
});

app.on("window-all-closed", () => {
  cleanup();
  if (process.platform !== "darwin") app.quit();
});

app.on("before-quit", () => {
  cleanup();
});

function registerIpc() {
  ipcMain.handle(
    "backend:request",
    async (_e, method: string, url: string, body?: Record<string, unknown>, timeoutMs?: number) => {
      return bridge.request(method, url, body, timeoutMs);
    },
  );

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
        try {
          fs.accessSync(p);
          return true;
        } catch {
          return false;
        }
      });

      let proc;
      if (command.startsWith("brew ")) {
        if (!brewPath) {
          resolve({
            ok: false,
            error: "Homebrew is not installed. Install it from https://brew.sh first.",
          });
          return;
        }
        const args = command.split(/\s+/).slice(1);
        proc = spawn(brewPath, args, { env });
      } else if (command.startsWith("script:")) {
        const scriptName = command.slice(7);
        const candidates = [
          path.join(path.dirname(app.getPath("exe")), "..", "scripts", scriptName),
          path.join(getMetalsharpDir(), "scripts", scriptName),
          path.join(__dirname, "..", "..", "..", "scripts", scriptName),
        ];
        const resolved = candidates.find((p) => fs.existsSync(p)) ?? null;
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

      const timer = setTimeout(
        () => {
          if (!settled) {
            settled = true;
            proc.kill();
            resolve({
              ok: false,
              error: "Installation timed out after 10 minutes.",
            });
          }
        },
        10 * 60 * 1000,
      );

      proc.stdout.on("data", (d: Buffer) => (stdout += d.toString()));
      proc.stderr.on("data", (d: Buffer) => (stderr += d.toString()));
      proc.on("error", (err: Error) => {
        if (!settled) {
          settled = true;
          clearTimeout(timer);
        }
        resolve({
          ok: false,
          error: `Failed to run "${command}": ${err.message}`,
        });
      });
      proc.on("close", (code: number) => {
        if (settled) return;
        settled = true;
        clearTimeout(timer);
        const combined = `${stdout}${stderr}`;
        const ok = code === 0 || combined.includes("already installed");
        resolve({
          ok,
          error: ok ? null : (combined.split("\n").filter(Boolean).pop() ?? `exit code ${code}`),
        });
      });
    });
  });

  ipcMain.handle("app:install-homebrew", async () => {
    const { exec } = require("child_process");
    return new Promise((resolve) => {
      const script = `/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"`;
      exec(
        `osascript -e 'tell application "Terminal" to do script "${script.replace(/"/g, '\\\\"')}"'`,
        (err: Error | null) => {
          if (err) {
            resolve({
              ok: false,
              error: "Failed to open Terminal for Homebrew install",
            });
          } else {
            resolve({
              ok: true,
              message: "Terminal opened — complete the Homebrew install there",
            });
          }
        },
      );
    });
  });

  ipcMain.handle("app:open-in-finder", async (_e, inputPath: string) => {
    const home = require("os").homedir();
    const resolved = inputPath.replace(/^~/, home);
    const fullPath = path.resolve(resolved);
    if (!fs.existsSync(fullPath)) {
      fs.mkdirSync(fullPath, { recursive: true });
    }
    shell.openPath(fullPath);
  });

  ipcMain.handle("backend:restart", async () => {
    return bridge.restart();
  });

  ipcMain.handle("backend:is-alive", async () => {
    return bridge.isAlive();
  });

  ipcMain.handle("updater:ensure-ready", async () => {
    return updaterBridge.ensureReady();
  });

  ipcMain.handle("updater:spawn-install", async (_e, dmgPath: string, backendPid: number, targetVersion: string) => {
    return updaterBridge.spawnInstallUpdater(dmgPath, backendPid, targetVersion);
  });

  ipcMain.handle("updater:install-status", async () => {
    return updaterBridge.readInstallStatus();
  });

  ipcMain.handle("updater:clear-status", async () => {
    updaterBridge.clearInstallStatus();
  });

  ipcMain.handle("backend:get-pid", async () => {
    return bridge.getBackendPid();
  });

  ipcMain.on("app:quit", () => {
    app.quit();
  });
}
