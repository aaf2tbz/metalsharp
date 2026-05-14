import { type ChildProcess, spawn } from "child_process";
import * as fs from "fs";
import * as http from "http";
import * as path from "path";

const STATUS_FILE = path.join(
  process.env.HOME || "/tmp",
  ".metalsharp",
  "update_install_status.json",
);

export interface InstallStatus {
  phase: string;
  percent: number;
  message: string;
  error: string | null;
  new_version: string | null;
  timestamp: number;
}

export class UpdaterBridge {
  private pythonPath: string | null = null;
  private scriptPath: string | null = null;
  private backendPort: number;
  private proc: ChildProcess | null = null;

  constructor(port: number = 9274) {
    this.backendPort = port;
  }

  async ensureReady(): Promise<boolean> {
    if (this.pythonPath && this.scriptPath) return true;

    const resourcesDir = process.resourcesPath || "";
    const devRoot = path.join(__dirname, "..", "..");

    const pythonCandidates = [
      path.join(resourcesDir, "updater", "bin", "python3"),
      path.join(resourcesDir, "updater", "python3"),
      path.join(devRoot, "updater", "bin", "python3"),
      "/usr/bin/python3",
      "/opt/homebrew/bin/python3",
      "/usr/local/bin/python3",
    ];

    for (const c of pythonCandidates) {
      try {
        fs.accessSync(c);
        this.pythonPath = c;
        break;
      } catch {}
    }

    if (!this.pythonPath) {
      console.error("Updater: no python3 found");
      return false;
    }

    const scriptCandidates = [
      path.join(resourcesDir, "updater", "update.py"),
      path.join(devRoot, "updater", "update.py"),
    ];

    for (const c of scriptCandidates) {
      try {
        fs.accessSync(c);
        this.scriptPath = c;
        break;
      } catch {}
    }

    if (!this.scriptPath) {
      console.error("Updater: update.py not found");
      return false;
    }

    return true;
  }

  async getBackendPid(): Promise<number | null> {
    return new Promise((resolve) => {
      const req = http.get(
        `http://127.0.0.1:${this.backendPort}/status`,
        (res) => {
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
        },
      );
      req.on("error", () => resolve(null));
      req.setTimeout(1500, () => {
        req.destroy();
        resolve(null);
      });
    });
  }

  spawnInstallUpdater(
    dmgPath: string,
    backendPid: number,
    targetVersion: string,
  ): { ok: boolean; error?: string } {
    if (!this.pythonPath || !this.scriptPath) {
      return { ok: false, error: "Updater not ready — python3 or update.py missing" };
    }

    if (!fs.existsSync(dmgPath)) {
      return { ok: false, error: "DMG file not found: " + dmgPath };
    }

    const args = [
      this.scriptPath,
      "--dmg", dmgPath,
      "--backend-pid", String(backendPid),
      "--target-version", targetVersion,
      "--status-file", STATUS_FILE,
      "--python", this.pythonPath,
    ];

    this.proc = spawn(this.pythonPath, args, {
      detached: true,
      stdio: "ignore",
      env: {
        ...process.env,
        PATH: [
          "/opt/homebrew/bin",
          "/usr/local/bin",
          "/usr/bin",
          "/bin",
          "/usr/sbin",
          "/sbin",
        ].join(":"),
      },
    });

    this.proc.unref();

    console.log(
      `Updater: spawned install updater (pid=${this.proc.pid}) for v${targetVersion}`,
    );

    return { ok: true };
  }

  readInstallStatus(): InstallStatus | null {
    try {
      const raw = fs.readFileSync(STATUS_FILE, "utf8");
      return JSON.parse(raw);
    } catch {
      return null;
    }
  }

  clearInstallStatus(): void {
    try {
      fs.unlinkSync(STATUS_FILE);
    } catch {}
  }

  static getStatusFilePath(): string {
    return STATUS_FILE;
  }
}
