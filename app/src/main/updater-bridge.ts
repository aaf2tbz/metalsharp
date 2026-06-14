import { spawn, spawnSync } from "child_process";
import * as fs from "fs";
import * as http from "http";
import * as os from "os";
import * as path from "path";

function getMetalsharpDir(): string {
  if (process.env.METALSHARP_HOME?.trim()) {
    return path.resolve(process.env.METALSHARP_HOME);
  }
  return path.join(os.homedir(), ".metalsharp");
}

function getStatusFile(): string {
  return path.join(getMetalsharpDir(), "update_install_status.json");
}

function getUpdaterPython(): string | null {
  const candidates = [
    process.env.METALSHARP_UPDATER_PYTHON,
    "/usr/bin/python3",
    "/opt/homebrew/bin/python3",
    "/usr/local/bin/python3",
    "python3",
  ].filter((candidate): candidate is string => !!candidate && candidate.trim().length > 0);

  for (const candidate of candidates) {
    if (candidate.includes(path.sep) && !fs.existsSync(candidate)) continue;
    const result = spawnSync(candidate, ["--version"], { stdio: "ignore" });
    if (!result.error && result.status === 0) return candidate;
  }

  return null;
}

export interface InstallStatus {
  phase: string;
  percent: number;
  message: string;
  error: string | null;
  new_version: string | null;
  timestamp: number;
}

export interface UpdaterReadyResult {
  ok: boolean;
  error?: string;
  scriptPath?: string;
  candidates?: string[];
}

export class UpdaterBridge {
  private scriptPath: string | null = null;
  private backendPort: number;

  constructor(port: number = 9274) {
    this.backendPort = port;
  }

  async ensureReady(): Promise<UpdaterReadyResult> {
    if (this.scriptPath && fs.existsSync(this.scriptPath)) {
      return { ok: true, scriptPath: this.scriptPath };
    }
    this.scriptPath = null;

    const resourcesDir = process.resourcesPath || "";
    const devRoot = path.join(__dirname, "..", "..");

    const candidates = [
      path.join(resourcesDir, "scripts", "tools", "updater", "update.py"),
      path.join(resourcesDir, "scripts", "tools", "updater", "update.sh"),
      path.join(resourcesDir, "updater", "update.py"),
      path.join(resourcesDir, "updater", "update.sh"),
      path.join(resourcesDir, "app.asar.unpacked", "updater", "update.py"),
      path.join(resourcesDir, "app.asar.unpacked", "updater", "update.sh"),
      path.join(devRoot, "updater", "update.py"),
      path.join(devRoot, "updater", "update.sh"),
    ];

    for (const c of candidates) {
      try {
        fs.accessSync(c, fs.constants.R_OK);
        this.scriptPath = c;
        break;
      } catch {}
    }

    if (!this.scriptPath) {
      const extracted = this.extractBundledUpdater(resourcesDir);
      if (extracted) this.scriptPath = extracted;
    }

    if (!this.scriptPath) {
      const error = `Updater handoff script not found. Checked: ${candidates.join(", ")}`;
      console.error(`Updater: ${error}`);
      return { ok: false, error, candidates };
    }

    return { ok: true, scriptPath: this.scriptPath, candidates };
  }

  async getBackendPid(): Promise<number | null> {
    return new Promise((resolve) => {
      const req = http.get(`http://127.0.0.1:${this.backendPort}/status`, (res) => {
        const chunks: Buffer[] = [];
        res.on("data", (c) => chunks.push(c));
        res.on("end", () => {
          try {
            const data = JSON.parse(Buffer.concat(chunks).toString());
            resolve(data.pid ?? null);
          } catch {
            resolve(null);
          }
        });
      });
      req.on("error", () => resolve(null));
      req.setTimeout(1500, () => {
        req.destroy();
        resolve(null);
      });
    });
  }

  spawnInstallUpdater(dmgPath: string, backendPid: number, targetVersion: string): { ok: boolean; error?: string } {
    if (!this.scriptPath) {
      return { ok: false, error: "Updater not ready — handoff script missing" };
    }

    if (!fs.existsSync(dmgPath)) {
      return { ok: false, error: `DMG file not found: ${dmgPath}` };
    }

    fs.mkdirSync(getMetalsharpDir(), { recursive: true });

    const args = [
      this.scriptPath,
      "--dmg",
      dmgPath,
      "--backend-pid",
      String(backendPid),
      "--target-version",
      targetVersion,
      "--status-file",
      getStatusFile(),
      "--metalsharp-home",
      getMetalsharpDir(),
      "--app-pid",
      String(process.pid),
    ];
    const python = this.scriptPath.endsWith(".py") ? getUpdaterPython() : null;
    if (this.scriptPath.endsWith(".py") && !python) {
      return { ok: false, error: "Updater not ready — python3 is unavailable" };
    }
    const command = python ?? "/bin/bash";

    const child = spawn(command, args, {
      detached: true,
      stdio: "ignore",
      env: {
        ...process.env,
        METALSHARP_HOME: getMetalsharpDir(),
        PATH: ["/opt/homebrew/bin", "/usr/local/bin", "/usr/bin", "/bin", "/usr/sbin", "/sbin"].join(":"),
      },
    });

    child.unref();

    console.log(`Updater: spawned handoff helper (pid=${child.pid}) for v${targetVersion}`);

    return { ok: true };
  }

  private extractBundledUpdater(resourcesDir: string): string | null {
    if (!resourcesDir) return null;
    const bundle = path.join(resourcesDir, "bundles", "metalsharp-scripts-tools.tar.zst");
    if (!fs.existsSync(bundle)) return null;

    const extractRoot = path.join(getMetalsharpDir(), "cache", "updater-tools");
    const pythonScript = path.join(extractRoot, "scripts", "tools", "updater", "update.py");
    const shellScript = path.join(extractRoot, "scripts", "tools", "updater", "update.sh");
    try {
      fs.rmSync(extractRoot, { recursive: true, force: true });
      fs.mkdirSync(extractRoot, { recursive: true });
      const result = spawnSync(
        "tar",
        [
          "--use-compress-program=unzstd",
          "-xf",
          bundle,
          "-C",
          extractRoot,
          "scripts/tools/updater/update.py",
          "scripts/tools/updater/update.sh",
        ],
        {
          env: {
            ...process.env,
            PATH: ["/opt/homebrew/bin", "/usr/local/bin", "/usr/bin", "/bin", "/usr/sbin", "/sbin"].join(":"),
          },
          stdio: "ignore",
        },
      );
      if (
        (result.status === 0 || fs.existsSync(pythonScript)) &&
        fs.existsSync(pythonScript) &&
        fs.statSync(pythonScript).size > 0
      ) {
        fs.chmodSync(pythonScript, 0o755);
        if (fs.existsSync(shellScript)) fs.chmodSync(shellScript, 0o755);
        return pythonScript;
      }
      if (fs.existsSync(shellScript) && fs.statSync(shellScript).size > 0) {
        fs.chmodSync(shellScript, 0o755);
        return shellScript;
      }
    } catch (error) {
      console.error("Updater: failed to extract bundled updater", error);
    }
    return null;
  }

  private static validateStatusPath(): boolean {
    const resolved = path.resolve(getStatusFile());
    return resolved.startsWith(path.resolve(getMetalsharpDir()));
  }

  readInstallStatus(): InstallStatus | null {
    try {
      if (!UpdaterBridge.validateStatusPath()) return null;
      const raw = fs.readFileSync(getStatusFile(), "utf8");
      return JSON.parse(raw);
    } catch {
      return null;
    }
  }

  clearInstallStatus(): void {
    try {
      if (!UpdaterBridge.validateStatusPath()) return;
      fs.unlinkSync(getStatusFile());
    } catch {}
  }

  static getStatusFilePath(): string {
    return getStatusFile();
  }
}
