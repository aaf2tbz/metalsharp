import { type ChildProcess, spawn } from "child_process";
import * as fs from "fs";
import * as http from "http";
import * as path from "path";

function getShellPath(): string {
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
  return [...additions, process.env.PATH].filter(Boolean).join(":");
}

const shellPath = getShellPath();

export class RustBridge {
  private proc: ChildProcess | null = null;
  private port: number = 9274;
  private base: string;

  constructor() {
    this.port = parseInt(process.env.METALSHARP_PORT || "9274", 10);
    this.base = `http://127.0.0.1:${this.port}`;
  }

  getPort(): number {
    return this.port;
  }

  async start(): Promise<void> {
    const binPath = this.findBinary();
    if (!binPath) {
      console.warn("metalsharp-backend binary not found, running in offline mode");
      return;
    }

    const needsRestart = await this.shouldRestart(binPath);
    if (needsRestart) {
      console.log("Backend version mismatch or not running — restarting...");
      this.killExisting();
      await new Promise((r) => setTimeout(r, 500));
    } else if (await this.isAlive()) {
      console.log("Backend already running and up to date");
      return;
    }

    this.spawnBackend(binPath);
    await this.waitForReady();
  }

  async restart(): Promise<{ ok: boolean; error?: string }> {
    const binPath = this.findBinary();
    if (!binPath) {
      return { ok: false, error: "Backend binary not found" };
    }

    this.stop();
    this.killExisting();
    await new Promise((r) => setTimeout(r, 500));

    try {
      this.spawnBackend(binPath);
      await this.waitForReady(10000);
      return { ok: true };
    } catch (e) {
      return { ok: false, error: (e as Error).message };
    }
  }

  stop() {
    this.proc?.kill();
    this.proc = null;
  }

  async isAlive(): Promise<boolean> {
    return new Promise((resolve) => {
      const req = http.get(`http://127.0.0.1:${this.port}/status`, (res) => {
        res.resume();
        resolve(true);
      });
      req.on("error", () => resolve(false));
      req.setTimeout(1500, () => {
        req.destroy();
        resolve(false);
      });
    });
  }

  async getBackendPid(): Promise<number | null> {
    return new Promise((resolve) => {
      const req = http.get(`http://127.0.0.1:${this.port}/status`, (res) => {
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

  async request(method: string, url: string, body?: Record<string, unknown>, timeoutMs?: number): Promise<unknown> {
    return new Promise((resolve, reject) => {
      const payload = body ? JSON.stringify(body) : "";
      const opts: http.RequestOptions = {
        hostname: "127.0.0.1",
        port: this.port,
        path: url,
        method,
        headers: {
          "Content-Type": "application/json",
          "Content-Length": Buffer.byteLength(payload),
        },
        timeout: timeoutMs ?? 30000,
      };

      const req = http.request(opts, (res) => {
        const chunks: Buffer[] = [];
        res.on("data", (c) => chunks.push(c));
        res.on("end", () => {
          try {
            resolve(JSON.parse(Buffer.concat(chunks).toString()));
          } catch {
            resolve({});
          }
        });
      });

      req.on("error", reject);
      req.on("timeout", () => {
        req.destroy();
        reject(new Error("request timeout"));
      });

      if (payload) req.write(payload);
      req.end();
    });
  }

  private spawnBackend(binPath: string) {
    this.proc = spawn(binPath, [], {
      env: {
        ...process.env,
        PATH: shellPath,
        METALSHARP_PORT: String(this.port),
      },
      stdio: ["ignore", "pipe", "pipe"],
    });

    this.proc.stdout?.on("data", (d: Buffer) => process.stdout.write(d));
    this.proc.stderr?.on("data", (d: Buffer) => process.stderr.write(d));

    this.proc.on("exit", (code) => {
      console.log(`metalsharp-backend exited with code ${code}`);
      this.proc = null;
    });
  }

  private async shouldRestart(binPath: string): Promise<boolean> {
    if (!(await this.isAlive())) return true;

    try {
      const binStat = fs.statSync(binPath);
      const runningPid = await this.getBackendPid();
      if (!runningPid) return false;

      const runningPath = await this.getProcessPath(runningPid);
      if (runningPath && runningPath !== binPath) return true;

      const runningMtime = runningPath ? fs.statSync(runningPath).mtimeMs : 0;
      if (binStat.mtimeMs > runningMtime + 1000) return true;
    } catch {}

    return false;
  }

  private async getProcessPath(pid: number): Promise<string | null> {
    if (!Number.isInteger(pid) || pid <= 0) return null;
    return new Promise((resolve) => {
      const { exec } = require("child_process");
      exec(`ps -o comm= -p ${Math.floor(pid)} 2>/dev/null`, (err: Error | null, stdout: string) => {
        if (err || !stdout.trim()) {
          resolve(null);
        } else {
          resolve(stdout.trim());
        }
      });
    });
  }

  private killExisting() {
    const { execSync } = require("child_process");
    try {
      execSync("pkill -x metalsharp-backend 2>/dev/null", { stdio: "ignore" });
    } catch {}
  }

  private findBinary(): string | null {
    const candidates = [
      path.join(process.resourcesPath || "", "metalsharp-backend"),
      path.join(__dirname, "..", "..", "src-rust", "target", "release", "metalsharp-backend"),
      path.join(__dirname, "..", "..", "src-rust", "target", "debug", "metalsharp-backend"),
      "/usr/local/bin/metalsharp-backend",
      "/usr/bin/metalsharp-backend",
    ];

    for (const c of candidates) {
      try {
        fs.accessSync(c);
        return c;
      } catch {}
    }
    return null;
  }

  private waitForReady(maxMs = 5000): Promise<void> {
    const start = Date.now();
    return new Promise((resolve, reject) => {
      const check = () => {
        if (Date.now() - start > maxMs) {
          reject(new Error("backend did not start in time"));
          return;
        }
        const req = http.get(`http://127.0.0.1:${this.port}/status`, (res) => {
          res.resume();
          resolve();
        });
        req.on("error", () => setTimeout(check, 200));
        req.setTimeout(1000);
      };
      setTimeout(check, 300);
    });
  }
}
