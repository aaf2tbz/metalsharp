#!/usr/bin/env node
import { spawnSync } from "node:child_process";
import { chmodSync, existsSync, mkdirSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const __dirname = dirname(fileURLToPath(import.meta.url));
const appRoot = resolve(__dirname, "..");
const source = resolve(appRoot, "../tools/process-manager/process-manager-helper.cpp");
const output = resolve(appRoot, "native/metalsharp-process-manager-helper");

if (!existsSync(source)) {
  console.warn(`[process-manager] helper source missing: ${source}`);
  process.exit(0);
}

mkdirSync(dirname(output), { recursive: true });

const candidates = process.platform === "darwin" ? ["/usr/bin/clang++", "clang++", "c++"] : ["c++", "clang++", "g++"];
let lastError = "";
for (const compiler of candidates) {
  const result = spawnSync(
    compiler,
    ["-std=c++17", "-O2", "-Wall", "-Wextra", source, "-o", output],
    { stdio: "pipe", encoding: "utf8" },
  );
  if (result.status === 0 && existsSync(output)) {
    chmodSync(output, 0o755);
    console.log(`[process-manager] built native helper: ${output}`);
    process.exit(0);
  }
  lastError = `${compiler}: ${result.stderr || result.stdout || result.error?.message || `exit ${result.status}`}`;
}

console.warn(`[process-manager] native helper not built; overlay will use JS fallback telemetry. ${lastError}`);
process.exit(0);
