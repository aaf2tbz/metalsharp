/**
 * Process Manager telemetry + IPC types (main-process side).
 *
 * `api-types.ts` is a global script consumed by the renderer (vite); the main
 * process is type-checked by `tsc`, which cannot `import type` from a
 * non-module. These shapes mirror the renderer ones and are structurally
 * compatible across the IPC boundary (the payload is JSON).
 */

export interface ProcessManagerProcess {
  pid: number;
  name: string;
  command: string;
  cpu_percent: number;
  mem_percent: number;
  fps: number | null;
  fps_fresh: boolean;
}

export interface ProcessManagerSample {
  ok: boolean;
  source: string;
  timestamp?: number;
  fps: number | null;
  fps_source?: string;
  fps_fresh?: boolean;
  cpu_percent: number;
  cpu_temp_c: number | null;
  cpu_temp_source?: string;
  cores_used: number;
  cores_total: number;
  ram_used_bytes: number;
  ram_total_bytes: number;
  gpu_percent: number | null;
  gpu_label?: string;
  renderer_percent?: number | null;
  gpu_mem_used_bytes?: number | null;
  gpu_mem_alloc_bytes?: number | null;
  chip?: string;
  processes: ProcessManagerProcess[];
  helper_path?: string;
}

export type ProcessManagerAction = "metalfx" | "gpu-acceleration" | "quit-game" | "view-steam";

export interface ProcessManagerActionResult {
  ok: boolean;
  error?: string;
  visualOnly?: boolean;
  action?: ProcessManagerAction;
  killed?: number[];
  errors?: string[];
  skippedSteamWinePids?: number;
  mode?: string;
}
