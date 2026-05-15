interface Game {
  id: string;
  name: string;
  exe_path: string;
  platform: string;
  steam_app_id?: number;
  size_bytes?: number;
  metalsharp_compatible: boolean;
  cover_art?: string;
}

interface SteamStatus {
  installed: boolean;
  path?: string;
  login_state?: LoginState;
  mac_installed?: boolean;
  running: boolean;
}

interface LoginState {
  state: "logged_in" | "logged_out" | "unknown";
  account?: { name: string; remembered: boolean }[] | null;
}

interface AppConfig {
  ok: boolean;
  launch_mode: "native" | "wine";
  wine_available: boolean;
  native_available: boolean;
}

interface UpdateStatus {
  ok: boolean;
  available: boolean;
  current_version: string;
  latest_version: string;
  download_url: string;
  release_notes: string;
  release_name: string;
  error?: string;
}

interface UpdateProgress {
  status: string;
  percent: number;
  message: string;
  error: string | null;
}

interface InstallStatus {
  phase: string;
  percent: number;
  message: string;
  error: string | null;
  new_version: string | null;
  timestamp: number;
}

interface CrashReportSummary {
  id: string;
  timestamp: string;
  game: string;
  exit_code: number;
}

interface SharpApp {
  id: string;
  name: string;
  exe_path: string;
  install_dir: string;
  cover: string | null;
  engine: string;
  installed_at: string;
  size_bytes: number;
}

interface BackendResponse {
  ok: boolean;
  data?: unknown;
  error?: string;
  pid?: number;
  games?: unknown[];
}

interface SetupState {
  ok: boolean;
  completed: boolean;
  step: number;
  deviceName: string;
  steamApiKeySet: boolean;
}

interface Dependency {
  id: string;
  name: string;
  desc: string;
  installed: boolean;
  required: boolean;
  installCmd: string;
}

interface DependenciesResponse {
  ok: boolean;
  allInstalled: boolean;
  dependencies: Dependency[];
}

type MetalsharpAPI = {
  request: (
    method: string,
    url: string,
    body?: Record<string, unknown>,
    timeoutMs?: number,
  ) => Promise<BackendResponse>;
  isFirstLaunch: () => Promise<boolean>;
  ejectDmg: () => Promise<void>;
  installDeps: (command: string) => Promise<{ ok: boolean; error?: string }>;
  installHomebrew: () => Promise<{ ok: boolean; error?: string }>;
  openInFinder: (path: string) => Promise<void>;
  restartBackend: () => Promise<{ ok: boolean; error?: string }>;
  isBackendAlive: () => Promise<boolean>;
  updaterEnsureReady: () => Promise<boolean>;
  updaterSpawnInstall: (
    dmgPath: string,
    backendPid: number,
    targetVersion: string,
  ) => Promise<{ ok: boolean; error?: string }>;
  updaterInstallStatus: () => Promise<InstallStatus | null>;
  updaterClearStatus: () => Promise<void>;
  backendGetPid: () => Promise<number | null>;
  quitApp: () => void;
  pickExeFile: () => Promise<string | null>;
  pickImageFile: () => Promise<string | null>;
};
