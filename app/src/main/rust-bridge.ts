import { ChildProcess, spawn } from "child_process";
import * as path from "path";
import * as http from "http";

export class RustBridge {
  private proc: ChildProcess | null = null;
  private port: number = 9274;
  private base: string;

  constructor() {
    this.port = parseInt(process.env.METALSHARP_PORT || "9274", 10);
    this.base = `http://127.0.0.1:${this.port}`;
  }

  async start(): Promise<void> {
    const binPath = this.findBinary();
    if (!binPath) {
      console.warn("metalsharp-backend binary not found, running in offline mode");
      return;
    }

    this.proc = spawn(binPath, [], {
      env: { ...process.env, METALSHARP_PORT: String(this.port) },
      stdio: ["ignore", "pipe", "pipe"],
    });

    this.proc.stdout?.on("data", (d: Buffer) => process.stdout.write(d));
    this.proc.stderr?.on("data", (d: Buffer) => process.stderr.write(d));

    this.proc.on("exit", (code) => {
      console.log(`metalsharp-backend exited with code ${code}`);
    });

    await this.waitForReady();
  }

  stop() {
    this.proc?.kill();
    this.proc = null;
  }

  async request(method: string, url: string, body?: Record<string, unknown>): Promise<unknown> {
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
        timeout: 30000,
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

  private findBinary(): string | null {
    const candidates = [
      path.join(process.resourcesPath || "", "metalsharp-backend"),
      path.join(__dirname, "..", "..", "src-rust", "target", "release", "metalsharp-backend"),
      path.join(__dirname, "..", "..", "src-rust", "target", "debug", "metalsharp-backend"),
      "/usr/local/bin/metalsharp-backend",
    ];

    for (const c of candidates) {
      try {
        require("fs").accessSync(c);
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
