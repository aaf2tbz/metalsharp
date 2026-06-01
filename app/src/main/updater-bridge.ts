import { spawn } from "child_process";
import * as fs from "fs";
import * as http from "http";
import * as os from "os";
import * as path from "path";

function getMetalsharpDir(): string {
  return path.join(os.homedir(), ".metalsharp");
}

const STATUS_FILE = path.join(getMetalsharpDir(), "update_install_status.json");

export interface InstallStatus {
  phase: string;
  percent: number;
  message: string;
  error: string | null;
  new_version: string | null;
  timestamp: number;
}

export class UpdaterBridge {
  private scriptPath: string | null = null;
  private backendPort: number;

  constructor(port: number = 9274) {
    this.backendPort = port;
  }

  async ensureReady(): Promise<boolean> {
    if (this.scriptPath) return true;

    const resourcesDir = process.resourcesPath || "";
    const devRoot = path.join(__dirname, "..", "..");

    const candidates = [
      path.join(resourcesDir, "scripts", "tools", "updater", "update.sh"),
      path.join(resourcesDir, "updater", "update.sh"),
      path.join(devRoot, "updater", "update.sh"),
    ];

    for (const c of candidates) {
      try {
        fs.accessSync(c);
        this.scriptPath = c;
        break;
      } catch {}
    }

    if (!this.scriptPath) {
      console.error("Updater: update.sh not found");
      return false;
    }

    return true;
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
      return { ok: false, error: "Updater not ready — update.sh missing" };
    }

    if (!fs.existsSync(dmgPath)) {
      return { ok: false, error: `DMG file not found: ${dmgPath}` };
    }

    const child = spawn(
      "/bin/bash",
      [
        this.scriptPath,
        "--dmg",
        dmgPath,
        "--backend-pid",
        String(backendPid),
        "--target-version",
        targetVersion,
        "--status-file",
        STATUS_FILE,
      ],
      {
        detached: true,
        stdio: "ignore",
        env: {
          ...process.env,
          PATH: ["/opt/homebrew/bin", "/usr/local/bin", "/usr/bin", "/bin", "/usr/sbin", "/sbin"].join(":"),
        },
      },
    );

    child.unref();

    console.log(`Updater: spawned install script (pid=${child.pid}) for v${targetVersion}`);

    return { ok: true };
  }

  private static validateStatusPath(): boolean {
    const resolved = path.resolve(STATUS_FILE);
    return resolved.startsWith(path.resolve(getMetalsharpDir()));
  }

  readInstallStatus(): InstallStatus | null {
    try {
      if (!UpdaterBridge.validateStatusPath()) return null;
      const raw = fs.readFileSync(STATUS_FILE, "utf8");
      return JSON.parse(raw);
    } catch {
      return null;
    }
  }

  clearInstallStatus(): void {
    try {
      if (!UpdaterBridge.validateStatusPath()) return;
      fs.unlinkSync(STATUS_FILE);
    } catch {}
  }

  static getStatusFilePath(): string {
    return STATUS_FILE;
  }
}
