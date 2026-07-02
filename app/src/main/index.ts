import { execFile, execFileSync, spawn } from "child_process";
import { app, BrowserWindow, clipboard, dialog, ipcMain, shell } from "electron";
import * as fs from "fs";
import * as http from "http";
import * as os from "os";
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

function isDevRuntime(): boolean {
  return process.env.METALSHARP_DEV === "1" || !app.isPackaged;
}

function isUiOnlyRuntime(): boolean {
  return process.env.METALSHARP_UI_ONLY === "1";
}

function getMetalsharpDir(): string {
  if (process.env.METALSHARP_HOME?.trim()) {
    return path.resolve(process.env.METALSHARP_HOME);
  }
  return path.join(os.homedir(), isDevRuntime() ? ".metalsharp-dev" : ".metalsharp");
}

function uiOnlyVersion(): string {
  return `${app.getVersion()}-ui`;
}

function uiOnlyBackendResponse(method: string, url: string): unknown {
  if (url === "/status") {
    return { ok: true, data: { ok: true, version: uiOnlyVersion() } };
  }
  if (url === "/setup/state") {
    return {
      ok: true,
      completed: true,
      step: 4,
      deviceName: "UI Preview Rig",
      steamApiKeySet: true,
      runtimeMigrationRequired: false,
    };
  }
  if (url === "/steam/api-key") {
    return { ok: true, key: "ui-only" };
  }
  if (url === "/steam/status") {
    return {
      ok: true,
      installed: true,
      running: false,
      mac_installed: true,
      mac_running: false,
      metalsharp_wine_available: true,
    };
  }
  if (url === "/steam/library") {
    const games = [
      {
        appid: 1583230,
        name: "High Fidelity Render Probe",
        installed: true,
        state: "installed",
        cover_url: "",
        header_url: "",
        size_bytes: 48791234560,
        launch_method: "m12",
        launch_method_name: "M12",
        preferred_pipeline: "m12",
      },
      {
        appid: 3527290,
        name: "PEAK",
        installed: true,
        state: "installed",
        cover_url: "",
        header_url: "",
        size_bytes: 6281222144,
        launch_method: "m11",
        launch_method_name: "M11",
        preferred_pipeline: "m11",
      },
      {
        appid: 105600,
        name: "Terraria",
        installed: false,
        state: "not_installed",
        cover_url: "",
        header_url: "",
        size_bytes: 629145600,
        launch_method: "auto",
        launch_method_name: "Auto",
        preferred_pipeline: null,
      },
      {
        appid: 1962700,
        name: "Shader Cache Lab",
        installed: true,
        state: "downloading",
        cover_url: "",
        header_url: "",
        size_bytes: 15032385536,
        launch_method: "m12",
        launch_method_name: "M12",
        preferred_pipeline: "m12",
      },
    ];
    return { ok: true, total: games.length, installed_count: 3, games };
  }
  if (url === "/scan") {
    return { ok: true, steam: { installed: true, running: false } };
  }
  if (url === "/update/check") {
    const version = uiOnlyVersion();
    return { ok: true, available: false, current_version: version, latest_version: version };
  }
  if (url === "/runtime/diagnostics") {
    const metalsharpHome = getMetalsharpDir();
    return {
      ok: true,
      schema: "metalsharp.runtime.diagnostics.v1",
      readOnly: true,
      summary: "UI preview runtime diagnostics: Wine 2.0 contracts, dxmt_m12 naming, and prefix policy are mocked ready.",
      paths: {
        metalsharpHome,
        runtimeRoot: path.join(metalsharpHome, "runtime"),
        wineRoot: path.join(metalsharpHome, "runtime", "wine"),
        wineBinary: path.join(metalsharpHome, "runtime", "wine", "bin", "metalsharp-wine"),
        steamPrefix: path.join(metalsharpHome, "prefix-steam"),
        gogPrefix: path.join(metalsharpHome, "bottles", "gog-prefix", "prefix"),
      },
      contracts: {
        schema: "metalsharp.runtime.contracts.v1",
        canonicalM12Surface: "dxmt_m12",
        canonicalM12InstalledPath: "runtime/wine/lib/dxmt_m12",
        canonicalM12Ok: true,
        total: 13,
        available: [
          "native_mono_arm64",
          "native_mono_x86",
          "m9",
          "m10",
          "m11",
          "m12_dxmt_m12",
          "wine_bare",
          "steam_background",
          "gogdl_wine",
        ],
        planned: ["dxvk_d9", "dxvk_d11", "vkd3d_d12"],
        external: ["d3dmetal_gptk"],
      },
      runtime: {
        ready: true,
        wineBinaryPresent: true,
        dxmtCurrent: true,
        dxmtM12Current: true,
        manifestOk: true,
        manifest: {
          ok: true,
          schema: "metalsharp.runtime.manifest.v1",
          manifestPath: path.join(metalsharpHome, "runtime", "metalsharp-runtime-manifest.json"),
          expected: {},
          persisted: { present: false },
          validation: { ok: true, checks: [] },
          artifacts: {},
        },
      },
      prefixes: {
        ok: true,
        steam: { id: "steam_background", path: path.join(metalsharpHome, "prefix-steam"), present: true },
        gog: {
          id: "gogdl_wine",
          path: path.join(metalsharpHome, "bottles", "gog-prefix", "prefix"),
          present: true,
          dedicatedPathOk: true,
          usesPrefixSteam: false,
        },
      },
      installReplacementGuard: {
        allowedNow: false,
        reason: "UI-only preview cannot authorize wiping or replacing an installed app.",
      },
      nextActions: ["Use a real backend build for runtime verification before replacing the current install."],
    };
  }
  if (url === "/steam/watch-steamapps") {
    return { ok: true, new_appids: [] };
  }
  if (url.startsWith("/mtsp/pipelines")) {
    return { ok: true, id: "m12", name: "M12", preferred: "m12", pipelines: [] };
  }
  if (method === "POST") {
    return { ok: true };
  }
  return { ok: true };
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

function appBundleFromExe(exePath: string): string | null {
  const match = exePath.match(/^(.*?\.app)\/Contents\/MacOS\//);
  return match?.[1] ?? null;
}

function installedMetalSharpAppPath(): string | null {
  const candidates = ["/Applications/MetalSharp.app", appBundleFromExe(app.getPath("exe"))].filter(
    (candidate): candidate is string => !!candidate && !candidate.startsWith("/Volumes/"),
  );

  return candidates.find((candidate) => fs.existsSync(candidate)) ?? null;
}

function deleteInstalledUpdateDmg(): string | null {
  const status = updaterBridge?.readInstallStatus?.();
  const dmgPath = status?.dmg_path ? path.resolve(status.dmg_path) : null;
  if (!dmgPath?.toLowerCase().endsWith(".dmg")) return null;
  if (!fs.existsSync(dmgPath)) return null;

  const msDir = path.resolve(getMetalsharpDir());
  const updatesDir = path.join(msDir, "cache", "updates");
  const allowed =
    dmgPath.startsWith(updatesDir + path.sep) || path.basename(dmgPath).toLowerCase().startsWith("metalsharp-");
  if (!allowed) return null;

  fs.rmSync(dmgPath, { force: true });
  return dmgPath;
}

function spawnFreshInstalledAppAfterExit(appPath: string) {
  const script = `current_pid=${process.pid}\napp_path=${JSON.stringify(appPath)}\nwhile kill -0 "$current_pid" 2>/dev/null; do sleep 0.2; done\n/usr/bin/open -n "$app_path"\n`;
  const child = spawn("/bin/sh", ["-c", script], {
    detached: true,
    stdio: "ignore",
    env: { ...process.env, PATH: ensureShellPath() },
  });
  child.unref();
}

function forceKillBackendProcesses() {
  for (const signal of ["TERM", "KILL"]) {
    try {
      execFileSync("/usr/bin/pkill", [`-${signal}`, "-x", "metalsharp-backend"], { stdio: "ignore" });
    } catch {}
    if (signal === "TERM") {
      try {
        execFileSync("/bin/sleep", ["0.5"]);
      } catch {}
    }
  }
}

type ProcessRow = { pid: number; comm: string; command: string };

function parseProcessRows(output: string): ProcessRow[] {
  return output
    .split(/\r?\n/)
    .map((line) => line.trim())
    .filter(Boolean)
    .map((line) => {
      const match = line.match(/^(\d+)\s+(\S+)\s+(.*)$/);
      if (!match) return null;
      return { pid: Number(match[1]), comm: match[2], command: match[3] };
    })
    .filter((row): row is ProcessRow => !!row && Number.isFinite(row.pid));
}

function isMetalSharpMainProcess(row: ProcessRow): boolean {
  const command = row.command.toLowerCase();
  const comm = path.basename(row.comm).toLowerCase();
  return (
    comm === "metalsharp" ||
    command.includes("/metalsharp.app/contents/macos/metalsharp") ||
    command.includes("/metalsharp.app/contents/macos/metalsharp ")
  );
}

function forceKillDuplicateMetalSharpApps(): { killed: number[]; errors: string[] } {
  const killed: number[] = [];
  const errors: string[] = [];
  let rows: ProcessRow[] = [];
  try {
    rows = parseProcessRows(execFileSync("/bin/ps", ["-axo", "pid=,comm=,command="], { encoding: "utf8" }));
  } catch (error) {
    const message = error instanceof Error ? error.message : String(error);
    return { killed, errors: [message] };
  }

  const duplicatePids = rows
    .filter(isMetalSharpMainProcess)
    .map((row) => row.pid)
    .filter((pid) => pid > 0 && pid !== process.pid);

  for (const pid of duplicatePids) {
    try {
      process.kill(pid, "SIGTERM");
      killed.push(pid);
    } catch (error) {
      const code =
        typeof error === "object" && error && "code" in error ? String((error as { code?: unknown }).code) : "";
      if (code !== "ESRCH") errors.push(`SIGTERM ${pid}: ${error instanceof Error ? error.message : String(error)}`);
    }
  }

  if (duplicatePids.length > 0) {
    for (let i = 0; i < 10; i++) {
      const stillAlive = duplicatePids.some((pid) => {
        try {
          process.kill(pid, 0);
          return true;
        } catch {
          return false;
        }
      });
      if (!stillAlive) break;
      try {
        execFileSync("/bin/sleep", ["0.25"]);
      } catch {}
    }
    for (const pid of duplicatePids) {
      try {
        process.kill(pid, "SIGKILL");
      } catch (error) {
        const code =
          typeof error === "object" && error && "code" in error ? String((error as { code?: unknown }).code) : "";
        if (code !== "ESRCH") errors.push(`SIGKILL ${pid}: ${error instanceof Error ? error.message : String(error)}`);
      }
    }
  }

  return { killed: [...new Set(killed)], errors };
}

async function checkNeedsMigration(): Promise<boolean> {
  const marker = hasPostUpdateMigrationMarker();
  return new Promise((resolve) => {
    const req = http.get("http://127.0.0.1:9274/update/migrate/check", (res) => {
      const chunks: Buffer[] = [];
      res.on("data", (c) => chunks.push(c));
      res.on("end", () => {
        try {
          const data = JSON.parse(Buffer.concat(chunks).toString());
          const needed = data.ok && data.needed === true;
          if (!needed && !marker.needed) clearPostUpdateMigrationMarker();
          resolve(needed);
        } catch {
          resolve(marker.needed);
        }
      });
    });
    req.on("error", () => resolve(marker.needed));
    req.setTimeout(3000, () => {
      req.destroy();
      resolve(marker.needed);
    });
  });
}

async function createWindow(migrating = false) {
  const uiOnly = isUiOnlyRuntime();
  mainWindow = new BrowserWindow({
    width: migrating ? 640 : 1200,
    height: migrating ? 420 : 800,
    minWidth: migrating ? 640 : 900,
    minHeight: migrating ? 420 : 600,
    resizable: !migrating,
    title: migrating ? "MetalSharp Migration" : "MetalSharp",
    backgroundColor: migrating ? "#1b2838" : uiOnly ? "#09070f" : undefined,
    frame: !uiOnly,
    transparent: false,
    vibrancy: migrating || uiOnly ? undefined : "sidebar",
    backgroundMaterial: migrating || uiOnly ? undefined : "acrylic",
    icon: path.join(__dirname, "..", "..", "build", "icon.png"),
    titleBarStyle: uiOnly ? undefined : "hiddenInset",
    trafficLightPosition: { x: 16, y: 16 },
    webPreferences: {
      preload: path.join(__dirname, "preload.js"),
      contextIsolation: true,
      nodeIntegration: false,
    },
  });

  mainWindow.loadFile(path.join(__dirname, "..", "renderer", "index.html"), {
    query: isUiOnlyRuntime() ? { theme: "developer" } : {},
  });
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

async function cleanup() {
  if (steamappsWatcher) {
    steamappsWatcher.close();
    steamappsWatcher = null;
  }
  await bridge?.killProcess();
}

let migrationMode = false;

app.whenReady().then(async () => {
  if (isUiOnlyRuntime()) {
    process.env.METALSHARP_DEV = "1";
    migrationMode = false;
    registerIpc();
    await createWindow(false);
    app.on("activate", () => {
      if (BrowserWindow.getAllWindows().length === 0) createWindow(false);
    });
    return;
  }

  process.env.METALSHARP_HOME = getMetalsharpDir();
  if (isDevRuntime()) process.env.METALSHARP_DEV = "1";
  ensureMetalsharpDirs();
  bridge = new RustBridge({ devMode: isDevRuntime(), metalsharpHome: getMetalsharpDir() });
  updaterBridge = new UpdaterBridge(bridge.getPort());
  const backendStart = await bridge.start();
  if (!backendStart.ok) {
    console.warn(`MetalSharp backend did not start during app launch: ${backendStart.error}`);
  }

  if (!backendStart.ok || !(await bridge.isAlive())) {
    console.log("Backend not responding after first start — retrying...");
    const retryStart = await bridge.start();
    if (!retryStart.ok) {
      console.warn(`MetalSharp backend retry failed: ${retryStart.error}`);
    }
  }

  const needsMigration = await checkNeedsMigration();
  if (needsMigration) {
    const duplicateKill = forceKillDuplicateMetalSharpApps();
    if (duplicateKill.killed.length > 0 || duplicateKill.errors.length > 0) {
      console.warn(
        `Migration guard handled duplicate MetalSharp apps: killed=${duplicateKill.killed.join(",") || "none"} errors=${duplicateKill.errors.join(" | ") || "none"}`,
      );
    }
  }
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

app.on("window-all-closed", async () => {
  await cleanup();
  if (process.platform !== "darwin") app.quit();
});

app.on("before-quit", async () => {
  await cleanup();
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
  if (isUiOnlyRuntime()) {
    return uiOnlyBackendResponse(method, url);
  }

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
    if (isUiOnlyRuntime()) return false;
    return isFirstLaunch();
  });

  ipcMain.handle("app:is-migration-mode", () => {
    return migrationMode;
  });

  ipcMain.handle("app:restart-after-migration", async () => {
    const appPath = installedMetalSharpAppPath();
    if (!appPath) {
      return { ok: false, error: "Installed MetalSharp.app was not found in /Applications." };
    }

    // Remove cached updater DMGs before tearing down the backend; this keeps the
    // user's disk from retaining the old installer image after a successful update.
    try {
      await requestMigrationBackend("POST", "/update/cleanup", undefined, 10000);
    } catch (error) {
      console.warn("Migration handoff: update cleanup request failed", error);
    }

    let deletedDmg: string | null = null;
    try {
      deletedDmg = deleteInstalledUpdateDmg();
    } catch (error) {
      console.warn("Migration handoff: failed to delete update DMG", error);
    }
    updaterBridge.clearInstallStatus();

    clearPostUpdateMigrationMarker();

    // User-requested order: close any leftover MetalSharp app instances first,
    // then kill the backend, then launch the freshly installed app bundle.
    const duplicateKill = forceKillDuplicateMetalSharpApps();
    if (duplicateKill.errors.length > 0) {
      console.warn(`Migration handoff app cleanup errors: ${duplicateKill.errors.join(" | ")}`);
    }

    await bridge.killProcess();
    forceKillBackendProcesses();
    spawnFreshInstalledAppAfterExit(appPath);

    setTimeout(() => app.exit(0), 50);
    return { ok: true, deletedDmg, launched: appPath };
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
    const home = os.homedir();
    const metalsharpDir = getMetalsharpDir();
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
    if (isUiOnlyRuntime()) return { ok: true, path: "ui-only://logs" };
    const logsPath = path.join(getMetalsharpDir(), "logs");
    fs.mkdirSync(logsPath, { recursive: true });
    await shell.openPath(logsPath);
    return { ok: true, path: logsPath };
  });

  ipcMain.handle("app:open-metalsharp-folder", async () => {
    if (isUiOnlyRuntime()) return { ok: true, path: "ui-only://metalsharp" };
    const metalsharpPath = getMetalsharpDir();
    fs.mkdirSync(metalsharpPath, { recursive: true });
    await shell.openPath(metalsharpPath);
    return { ok: true, path: metalsharpPath };
  });

  ipcMain.handle("app:repair-data-access", async () => {
    if (isUiOnlyRuntime()) return { ok: true, path: "ui-only://metalsharp", checks: [] };
    return verifyMetalsharpDataAccess();
  });

  ipcMain.handle("app:copy-text", async (_e, text: string) => {
    clipboard.writeText(text);
    return { ok: true };
  });

  ipcMain.handle("backend:restart", async () => {
    if (isUiOnlyRuntime()) return { ok: true };
    return bridge.restart();
  });

  ipcMain.handle("backend:is-alive", async () => {
    if (isUiOnlyRuntime()) return true;
    return bridge.isAlive();
  });

  ipcMain.handle("updater:ensure-ready", async () => {
    if (isUiOnlyRuntime()) return { ok: false, error: "Updater is disabled in UI-only preview mode." };
    return updaterBridge.ensureReady();
  });

  ipcMain.handle("updater:spawn-install", async (_e, dmgPath: string, backendPid: number, targetVersion: string) => {
    if (isUiOnlyRuntime()) return { ok: false, error: "Updater is disabled in UI-only preview mode." };
    return updaterBridge.spawnInstallUpdater(dmgPath, backendPid, targetVersion);
  });

  ipcMain.handle("updater:install-status", async () => {
    if (isUiOnlyRuntime()) return null;
    return updaterBridge.readInstallStatus();
  });

  ipcMain.handle("updater:clear-status", async () => {
    if (isUiOnlyRuntime()) return;
    updaterBridge.clearInstallStatus();
  });

  ipcMain.handle("backend:get-pid", async () => {
    if (isUiOnlyRuntime()) return null;
    return bridge.getBackendPid();
  });

  ipcMain.handle("migrate:check", async () => {
    if (isUiOnlyRuntime()) return { ok: true, needed: false };
    return requestMigrationBackend("GET", "/update/migrate/check");
  });

  ipcMain.handle("migrate:start", async () => {
    if (isUiOnlyRuntime()) return { ok: true };
    return requestMigrationBackend("POST", "/update/migrate/start", undefined, 10000);
  });

  ipcMain.handle("migrate:progress", async () => {
    if (isUiOnlyRuntime()) return { ok: true, phase: "complete", percent: 100 };
    return requestMigrationBackend("GET", "/update/migrate/progress");
  });

  ipcMain.on("app:quit", () => {
    app.quit();
  });

  ipcMain.on("app:uninstall", async () => {
    if (!mainWindow) return;
    const choice = await dialog.showMessageBox(mainWindow, {
      type: "warning",
      title: "Uninstall MetalSharp",
      message: "Are you sure you want to uninstall MetalSharp?",
      detail:
        "This will permanently delete all Wine prefixes, bottles, game data, " +
        "Steam installation, Wine runtime, shader caches, and all settings. " +
        "MetalSharp will also be removed from Applications and moved to Trash. " +
        "This action cannot be undone.",
      buttons: ["Cancel", "Uninstall"],
      defaultId: 0,
      cancelId: 0,
      noLink: true,
    });
    if (choice.response !== 1) return;

    await cleanup();

    const msDir = getMetalsharpDir();
    let dataRemoved = false;
    try {
      fs.rmSync(msDir, { recursive: true, force: true });
      dataRemoved = true;
    } catch (e) {
      console.error("Failed to remove MetalSharp data:", e);
    }

    // Spawn a detached shell that waits for this process to exit, then
    // moves the app bundle to the macOS Trash.
    const exePath = app.getPath("exe");
    const appBundle = exePath.match(/^(\/Applications\/[^/]+\.app)\//)?.[1];
    if (appBundle && fs.existsSync(appBundle)) {
      const pid = process.pid;
      const script = `
        while kill -0 ${pid} 2>/dev/null; do sleep 0.5; done
        osascript -e 'tell application "Finder" to delete POSIX file "${appBundle}"'
      `;
      const { spawn } = require("child_process");
      spawn("/bin/bash", ["-c", script], { detached: true, stdio: "ignore" }).unref();
    }

    // Show success dialog — user must close the app to finish.
    if (mainWindow && !mainWindow.isDestroyed()) {
      await dialog.showMessageBox(mainWindow, {
        type: "info",
        title: "MetalSharp Uninstalled",
        message: "MetalSharp data has been removed successfully.",
        detail:
          "All Wine prefixes, bottles, Steam, runtime, and settings have been deleted. " +
          (appBundle
            ? "When you close this window, MetalSharp will be moved to the Trash."
            : "Close this window to exit MetalSharp.") +
          "\n\nClick OK to close the app.",
        buttons: ["OK"],
        defaultId: 0,
        noLink: true,
      });
    }

    app.quit();
  });

  ipcMain.handle("gog:oauth-login", async (_e, authUrl: string) => {
    if (!mainWindow) return { ok: false, error: "Main window is not ready." };
    let parsed: URL;
    try {
      parsed = new URL(authUrl);
    } catch {
      return { ok: false, error: "Invalid GOG auth URL." };
    }
    if (parsed.hostname !== "auth.gog.com") {
      return { ok: false, error: "Refusing to open non-GOG auth URL." };
    }

    return new Promise<{ ok: boolean; code?: string; redirectUrl?: string; error?: string }>((resolve) => {
      let settled = false;
      let safariPoll: ReturnType<typeof setInterval> | null = null;
      const safariDeadline = Date.now() + 5 * 60 * 1000;

      const stopSafariPolling = () => {
        if (safariPoll) clearInterval(safariPoll);
        safariPoll = null;
      };

      const finish = (result: { ok: boolean; code?: string; redirectUrl?: string; error?: string }) => {
        if (settled) return;
        settled = true;
        stopSafariPolling();
        resolve(result);
      };

      const runAppleScript = (script: string, args: string[] = []) =>
        new Promise<string>((resolveScript, rejectScript) => {
          execFile("/usr/bin/osascript", ["-e", script, ...args], { timeout: 5000 }, (error, stdout) => {
            if (error) rejectScript(error);
            else resolveScript(stdout.toString());
          });
        });

      const safariUrlsScript = `
tell application "Safari"
  if (count of windows) = 0 then return ""
  set output to ""
  repeat with w in windows
    repeat with t in tabs of w
      try
        set output to output & (URL of t as text) & linefeed
      end try
    end repeat
  end repeat
  return output
end tell`;

      const closeSafariUrlScript = `
on run argv
  set targetUrl to item 1 of argv
  tell application "Safari"
    repeat with w in windows
      repeat with i from (count of tabs of w) to 1 by -1
        set t to tab i of w
        try
          if (URL of t as text) is targetUrl then
            close t
            if (count of tabs of w) is 0 then close w
            return "closed"
          end if
        end try
      end repeat
    end repeat
  end tell
  return "not_found"
end run`;

      const closeSafariUrl = (url: string) => {
        runAppleScript(closeSafariUrlScript, [url]).catch(() => undefined);
      };

      const gogCodeFromUrl = (redirect: URL) => {
        const queryCode = redirect.searchParams.get("code");
        if (queryCode) return queryCode;
        const hash = redirect.hash.startsWith("#") ? redirect.hash.slice(1) : redirect.hash;
        return new URLSearchParams(hash).get("code");
      };

      const isGogHost = (redirect: URL) => {
        const host = redirect.hostname.toLowerCase();
        return host === "gog.com" || host.endsWith(".gog.com");
      };

      const isGogCallbackUrl = (redirect: URL) => {
        if (!isGogHost(redirect)) return false;
        const path = redirect.pathname.toLowerCase().replace(/\/+$/, "");
        return path.includes("on_login_success") || Boolean(gogCodeFromUrl(redirect));
      };

      const inspectUrl = (url: string) => {
        try {
          const redirect = new URL(url);
          if (!isGogCallbackUrl(redirect)) return false;
          const code = gogCodeFromUrl(redirect);
          const callbackError = redirect.searchParams.get("error") ?? redirect.searchParams.get("error_description");
          if (callbackError) {
            finish({ ok: false, error: `GOG login callback failed: ${callbackError}`, redirectUrl: url });
            return true;
          }
          if (!code) return true;
          closeSafariUrl(url);
          finish({ ok: true, code, redirectUrl: url });
          return true;
        } catch {
          return false;
        }
      };

      const pollSafari = async () => {
        if (settled) return;
        if (Date.now() > safariDeadline) {
          finish({ ok: false, error: "Timed out waiting for the GOG login callback in Safari." });
          return;
        }
        try {
          const output = await runAppleScript(safariUrlsScript);
          for (const url of output
            .split(/\r?\n/)
            .map((line) => line.trim())
            .filter(Boolean)) {
            if (inspectUrl(url)) return;
          }
        } catch {}
      };

      safariPoll = setInterval(() => {
        pollSafari().catch(() => undefined);
      }, 500);
      shell
        .openExternal(authUrl)
        .then(() => pollSafari().catch(() => undefined))
        .catch((error) => finish({ ok: false, error: error.message, redirectUrl: authUrl }));
    });
  });

  ipcMain.handle("app:pick-directory", async (_e, title?: string) => {
    if (!mainWindow) return null;
    const result = await dialog.showOpenDialog(mainWindow, {
      title: title || "Select a folder",
      properties: ["openDirectory", "createDirectory"],
    });
    if (result.canceled || result.filePaths.length === 0) return null;
    return result.filePaths[0];
  });

  ipcMain.handle("app:pick-exe-file", async () => {
    if (!mainWindow) return null;
    const result = await dialog.showOpenDialog(mainWindow, {
      title: "Select a Windows installer or executable",
      properties: ["openFile"],
      filters: [{ name: "Windows App", extensions: ["exe", "msi"] }],
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
