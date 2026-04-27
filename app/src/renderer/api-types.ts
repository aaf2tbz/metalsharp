interface Game {
  id: string;
  name: string;
  exe_path: string;
  platform: string;
  steam_app_id?: number;
  size_bytes?: number;
  metalsharp_compatible: boolean;
}

interface SteamStatus {
  installed: boolean;
  path?: string;
  mac_installed?: boolean;
  steam_cmd_path?: string;
  running: boolean;
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
