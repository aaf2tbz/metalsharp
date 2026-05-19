import { app, BrowserWindow, clipboard, dialog, ipcMain, shell } from "electron";
import * as fs from "fs";
import * as http from "http";
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
    "/usr/local/sbin",
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
  const dirs = [
    "games",
    "cache",
    "logs",
    "sharp-library",
    "shader-cache",
    "pipeline-cache",
    "runtime/fna",
    "runtime/shims",
    "runtime/mono-x86",
    "runtime/dxvk-1.10.3",
  ];
  for (const d of dirs) {
    fs.mkdirSync(path.join(base, d), { recursive: true });
  }
}

function verifyMetalsharpDataAccess() {
  ensureMetalsharpDirs();
  const base = getMetalsharpDir();
  const checks = ["logs", "sharp-library", "cache"].map((dir) => {
    const checkPath = path.join(base, dir, ".metalsharp-access-check");
    try {
      fs.writeFileSync(checkPath, String(Date.now()));
      fs.unlinkSync(checkPath);
      return { dir, ok: true };
    } catch (error) {
      const message = error instanceof Error ? error.message : "Unknown filesystem error";
      return { dir, ok: false, error: message };
    }
  });
  return { ok: checks.every((check) => check.ok), path: base, checks };
}

function hasPostUpdateMigrationMarker(): { needed: boolean; targetVersion?: string } {
  const markerPath = path.join(getMetalsharpDir(), ".post-update-migration");
  try {
    if (!fs.existsSync(markerPath)) return { needed: false };
    const data = JSON.parse(fs.readFileSync(markerPath, "utf8"));
    return { needed: data.needed === true, targetVersion: data.target_version };
  } catch {
    return { needed: false };
  }
}

function clearPostUpdateMigrationMarker() {
  const markerPath = path.join(getMetalsharpDir(), ".post-update-migration");
  try {
    fs.unlinkSync(markerPath);
  } catch {}
}

async function checkNeedsMigration(): Promise<boolean> {
  const marker = hasPostUpdateMigrationMarker();
  if (marker.needed) {
    return true;
  }

  return new Promise((resolve) => {
    const req = http.get("http://127.0.0.1:9274/update/migrate/check", (res) => {
      const chunks: Buffer[] = [];
      res.on("data", (c) => chunks.push(c));
      res.on("end", () => {
        try {
          const data = JSON.parse(Buffer.concat(chunks).toString());
          resolve(data.ok && data.needed === true);
        } catch {
          resolve(false);
        }
      });
    });
    req.on("error", () => resolve(false));
    req.setTimeout(3000, () => {
      req.destroy();
      resolve(false);
    });
  });
}

async function createWindow(migrating = false) {
  mainWindow = new BrowserWindow({
    width: migrating ? 640 : 1200,
    height: migrating ? 420 : 800,
    minWidth: migrating ? 640 : 900,
    minHeight: migrating ? 420 : 600,
    resizable: !migrating,
    title: migrating ? "MetalSharp Migration" : "MetalSharp",
    backgroundColor: "#1b2838",
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

let migrationMode = false;

app.whenReady().then(async () => {
  ensureMetalsharpDirs();
  bridge = new RustBridge();
  updaterBridge = new UpdaterBridge(bridge.getPort());
  const backendStart = await bridge.start();
  if (!backendStart.ok) {
    console.warn(`MetalSharp backend did not start during app launch: ${backendStart.error}`);
  }

  const needsMigration = await checkNeedsMigration();
  migrationMode = needsMigration;

  registerIpc();

  await createWindow(needsMigration);

  if (!needsMigration) {
    startSteamappsWatcher();
  }

  app.on("activate", () => {
    if (BrowserWindow.getAllWindows().length === 0) createWindow(false);
  });
});

app.on("window-all-closed", () => {
  cleanup();
  if (process.platform !== "darwin") app.quit();
});

app.on("before-quit", () => {
  cleanup();
});

function backendErrorMessage(error: unknown): string {
  if (error instanceof Error) return error.message;
  if (typeof error === "string") return error;
  return "Backend request failed";
}

async function requestBackend(
  method: string,
  url: string,
  body?: Record<string, unknown>,
  timeoutMs?: number,
): Promise<unknown> {
  const ready = await bridge.ensureRunning();
  if (!ready.ok) {
    return { ok: false, error: ready.error ?? "Backend is not available" };
  }

  try {
    return await bridge.request(method, url, body, timeoutMs);
  } catch (e) {
    return { ok: false, error: backendErrorMessage(e) };
  }
}

async function requestMigrationBackend(
  method: string,
  url: string,
  body?: Record<string, unknown>,
  timeoutMs?: number,
): Promise<unknown> {
  const ready = await bridge.ensureRunning(30000);
  if (!ready.ok) {
    return {
      ok: false,
      error: `Migration backend unavailable: ${ready.error ?? "could not start metalsharp-backend"}`,
    };
  }

  return requestBackend(method, url, body, timeoutMs);
}

function registerIpc() {
  ipcMain.handle(
    "backend:request",
    async (_e, method: string, url: string, body?: Record<string, unknown>, timeoutMs?: number) => {
      return requestBackend(method, url, body, timeoutMs);
    },
  );

  ipcMain.handle("app:is-first-launch", () => {
    return isFirstLaunch();
  });

  ipcMain.handle("app:is-migration-mode", () => {
    return migrationMode;
  });

  ipcMain.handle("app:restart-after-migration", async () => {
    clearPostUpdateMigrationMarker();
    app.relaunch();
    app.exit(0);
  });

  ipcMain.handle("app:eject-dmg", async () => {
    if (process.platform !== "darwin") {
      return { ok: false, error: "DMG ejection is only available on macOS." };
    }

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
    if (process.platform !== "darwin") {
      return { ok: false, error: "Homebrew setup is only available on macOS." };
    }

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
    const metalsharpDir = path.join(home, ".metalsharp");
    const resolved = inputPath.replace(/^~/, home);
    const fullPath = path.resolve(resolved);
    if (!fullPath.startsWith(metalsharpDir) && !fullPath.startsWith(home)) {
      return;
    }
    if (!fs.existsSync(fullPath)) {
      fs.mkdirSync(fullPath, { recursive: true });
    }
    shell.openPath(fullPath);
  });

  ipcMain.handle("app:open-logs-folder", async () => {
    const logsPath = path.join(app.getPath("home"), ".metalsharp", "logs");
    fs.mkdirSync(logsPath, { recursive: true });
    await shell.openPath(logsPath);
    return { ok: true, path: logsPath };
  });

  ipcMain.handle("app:open-metalsharp-folder", async () => {
    const metalsharpPath = getMetalsharpDir();
    fs.mkdirSync(metalsharpPath, { recursive: true });
    await shell.openPath(metalsharpPath);
    return { ok: true, path: metalsharpPath };
  });

  ipcMain.handle("app:repair-data-access", async () => {
    return verifyMetalsharpDataAccess();
  });

  ipcMain.handle("app:copy-text", async (_e, text: string) => {
    clipboard.writeText(text);
    return { ok: true };
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

  ipcMain.handle("migrate:check", async () => {
    return requestMigrationBackend("GET", "/update/migrate/check");
  });

  ipcMain.handle("migrate:start", async () => {
    return requestMigrationBackend("POST", "/update/migrate/start", undefined, 10000);
  });

  ipcMain.handle("migrate:progress", async () => {
    return requestMigrationBackend("GET", "/update/migrate/progress");
  });

  ipcMain.on("app:quit", () => {
    app.quit();
  });

  ipcMain.handle("app:pick-exe-file", async () => {
    if (!mainWindow) return null;
    const result = await dialog.showOpenDialog(mainWindow, {
      title: "Select an EXE file",
      properties: ["openFile"],
      filters: [{ name: "Windows Executable", extensions: ["exe"] }],
    });
    if (result.canceled || result.filePaths.length === 0) return null;
    return result.filePaths[0];
  });

  ipcMain.handle("app:pick-image-file", async () => {
    if (!mainWindow) return null;
    const result = await dialog.showOpenDialog(mainWindow, {
      title: "Select a cover image",
      properties: ["openFile"],
      filters: [{ name: "Image", extensions: ["jpg", "jpeg", "png"] }],
    });
    if (result.canceled || result.filePaths.length === 0) return null;
    return result.filePaths[0];
  });
}
