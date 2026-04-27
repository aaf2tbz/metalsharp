export interface Game {
  id: string;
  name: string;
  exePath: string;
  icon?: string;
  platform: "steam" | "local" | "epic" | "gog";
  steamAppId?: number;
  sizeBytes?: number;
  lastPlayed?: string;
  coverArt?: string;
  metalsharpCompatible: boolean;
}

export interface SteamStatus {
  installed: boolean;
  path?: string;
  steamCmdPath?: string;
  running: boolean;
}

export interface LaunchOptions {
  fullscreen: boolean;
  debugMetal: boolean;
  verbose: boolean;
  prefix?: string;
  customArgs: string[];
}

export interface RustResponse<T = unknown> {
  ok: boolean;
  data?: T;
  error?: string;
}

export interface ScanResult {
  games: Game[];
  steam: SteamStatus;
}
