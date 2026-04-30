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
  steam_cmd_path?: string;
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
  available: boolean;
  current_version: string;
  latest_version: string;
  download_url: string;
  release_notes: string;
}

interface CrashReportSummary {
  id: string;
  timestamp: string;
  game: string;
  exit_code: number;
}

interface BackendResponse {
  ok: boolean;
  data?: unknown;
  error?: string;
  pid?: number;
  games?: unknown[];
}

type MetalsharpAPI = {
  request: (method: string, url: string, body?: Record<string, unknown>) => Promise<BackendResponse>;
};
