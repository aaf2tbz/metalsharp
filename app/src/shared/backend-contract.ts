// Generated-facing types for contracts/electron-backend.v1.json. Keep this
// module implementation-independent: it describes the Electron/backend wire
// protocol, not Rust or generated-C internals.

export const BACKEND_CONTRACT_VERSION = "1" as const;

export interface BackendStatus {
  ok: boolean;
  version: string;
  contract_version?: string;
  pid: number;
  dev_mode?: boolean;
  metalsharp_home: string;
}

export interface SetupStateContract {
  ok: boolean;
  completed: boolean;
  step: number;
  steamApiKeySet: boolean;
}

export interface SteamStatusContract {
  installed: boolean;
  running: boolean;
}

export interface SteamLibraryContract {
  ok: boolean;
  games: unknown[];
  total: number;
}

export interface BottleListContract {
  ok: boolean;
  bottles: unknown[];
}

export interface LogListContract {
  ok: boolean;
  logs: unknown[];
}

export interface LogStreamContract {
  ok: boolean;
  lines: string[];
  total: number;
}

export interface PipelineDryRunContract {
  appid: number;
  dry_run: boolean;
  env_pairs: Array<{ key: string; value: string }>;
}

export interface LaunchAutoRequest {
  appid: number;
  pipeline?: string;
}

export interface LaunchResult {
  ok: boolean;
  error?: string;
  pid?: number;
}

export type ContractMethod = "GET" | "POST";

export interface ContractResponses {
  "GET /status": BackendStatus;
  "GET /setup/state": SetupStateContract;
  "GET /steam/status": SteamStatusContract;
  "GET /steam/library": SteamLibraryContract;
  "GET /bottles": BottleListContract;
  "GET /logs": LogListContract;
  "GET /logs/stream": LogStreamContract;
  "GET /diagnostics/pipeline/dry-run": PipelineDryRunContract;
  "GET /diagnostics/m12/dry-run": PipelineDryRunContract;
  "POST /game/launch-auto": LaunchResult;
}

export type ContractRoute = keyof ContractResponses;

export const LEGACY_CONTRACT_BY_BACKEND_VERSION: Readonly<Record<string, string>> = {
  "0.54.5": BACKEND_CONTRACT_VERSION,
  "0.55.0": BACKEND_CONTRACT_VERSION,
};

export function compatibleContractVersion(
  status: Pick<BackendStatus, "version" | "contract_version">,
): string | undefined {
  return status.contract_version ?? LEGACY_CONTRACT_BY_BACKEND_VERSION[status.version];
}
