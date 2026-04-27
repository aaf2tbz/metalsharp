/// <reference path="./api-types.ts" />

function getAPI(): MetalsharpAPI {
  return (window as unknown as { metalsharp: MetalsharpAPI }).metalsharp;
}

class App {
  private games: Game[] = [];
  private steam: SteamStatus | null = null;
  private config: AppConfig | null = null;
  private runningPid: number | null = null;
  private runningGameId: string | null = null;
  private currentView = "library";

  async init() {
    this.bindNav();
    await this.checkBackend();
    await this.loadConfig();
    await this.scan();
  }

  private bindNav() {
    document.querySelectorAll(".nav-item").forEach((btn) => {
      btn.addEventListener("click", () => {
        const view = (btn as HTMLElement).dataset.view!;
        this.switchView(view);
      });
    });
  }

  private switchView(view: string) {
    this.currentView = view;
    document.querySelectorAll(".nav-item").forEach((b) => b.classList.remove("active"));
    document.querySelector(`[data-view="${view}"]`)?.classList.add("active");
    document.querySelectorAll(".view").forEach((v) => v.classList.add("hidden"));
    document.getElementById(`view-${view}`)?.classList.remove("hidden");

    if (view === "settings") this.renderSettings();
    if (view === "store") this.renderStore();
  }

  private async api<T = unknown>(method: string, url: string, body?: Record<string, unknown>): Promise<T | null> {
    try {
      const res = await getAPI().request(method, url, body);
      if (!res.ok && res.error) this.toast(res.error, "error");
      return (res.data ?? res) as T;
    } catch (e) {
      this.toast(`Backend error: ${(e as Error).message}`, "error");
      return null;
    }
  }

  private async checkBackend() {
    const dot = document.getElementById("backend-status")!;
    const text = document.getElementById("backend-status-text")!;
    try {
      const res = await getAPI().request("GET", "/status");
      if (res.ok) {
        dot.classList.add("connected");
        text.textContent = "Connected";
      }
    } catch {
      dot.classList.add("error");
      text.textContent = "Offline";
    }
  }

  private async loadConfig() {
    const cfg = await this.api<AppConfig>("GET", "/config");
    if (cfg) this.config = cfg;
  }

  private async scan() {
    const result = await this.api<{ games: Game[]; steam: SteamStatus }>("GET", "/scan");
    if (result) {
      this.games = result.games ?? [];
      this.steam = result.steam ?? { installed: false, running: false };
    }
    this.renderLibrary();
  }

  private launchMode(): string {
    return this.config?.launch_mode ?? "native";
  }

  private renderLibrary() {
    const el = document.getElementById("view-library")!;
    if (this.games.length === 0) {
      el.innerHTML = `
        <div class="library-header">
          <div>
            <h1>Library</h1>
            <p class="subtitle">No games detected yet</p>
          </div>
          <div class="header-actions">
            <button class="btn btn-secondary" id="btn-scan">Scan</button>
            <button class="btn btn-primary" id="btn-add-game">Add Game</button>
          </div>
        </div>
        <div class="empty-state">
          <div class="empty-state-icon">&#x2699;</div>
          <h2>No games found</h2>
          <p>Install Steam or add Windows executables to ~/.metalsharp/games/ to see them here.</p>
        </div>
      `;
      el.querySelector("#btn-scan")?.addEventListener("click", () => this.scan());
      el.querySelector("#btn-add-game")?.addEventListener("click", () => this.switchView("store"));
      return;
    }

    el.innerHTML = `
      <div class="library-header">
        <div>
          <h1>Library</h1>
          <p class="subtitle">${this.games.length} game${this.games.length !== 1 ? "s" : ""} detected</p>
        </div>
        <div class="header-actions">
          <button class="btn btn-secondary" id="btn-scan">Refresh</button>
        </div>
      </div>
      <div class="game-grid" id="game-grid"></div>
    `;

    const grid = el.querySelector("#game-grid")!;
    for (const game of this.games) {
      grid.appendChild(this.createGameCard(game));
    }

    el.querySelector("#btn-scan")?.addEventListener("click", () => this.scan());
  }

  private createGameCard(game: Game): HTMLElement {
    const card = document.createElement("div");
    card.className = "game-card";
    card.dataset.gameId = game.id;

    const isRunning = this.runningGameId === game.id;
    if (isRunning) card.classList.add("running");

    const sizeStr = game.size_bytes ? this.formatBytes(game.size_bytes) : "";
    const compatBadge = game.metalsharp_compatible
      ? `<span class="badge badge-ok">Compatible</span>`
      : `<span class="badge badge-warn">Unknown</span>`;

    card.innerHTML = `
      <div class="game-card-banner">
        <span class="game-icon-placeholder">${this.platformIcon(game.platform)}</span>
      </div>
      <div class="game-card-body">
        <div class="game-card-title">${this.esc(game.name)}</div>
        <div class="game-card-meta">
          <span class="game-card-platform">${game.platform}</span>
          ${sizeStr ? `<span class="game-card-size">${sizeStr}</span>` : ""}
          ${compatBadge}
        </div>
        <div class="game-card-actions">
          <button class="btn btn-play ${isRunning ? "running" : ""}" data-action="${isRunning ? "stop" : "play"}" data-id="${game.id}">
            ${isRunning ? "Stop" : "Play"}
          </button>
        </div>
      </div>
    `;

    card.querySelector("[data-action]")?.addEventListener("click", (e) => {
      const btn = e.currentTarget as HTMLElement;
      const action = btn.dataset.action;
      if (action === "play") this.launchGame(game);
      else if (action === "stop" && this.runningPid) this.killGame(game);
    });

    return card;
  }

  private async launchGame(game: Game) {
    this.toast(`Launching ${game.name}...`, "success");
    const result = await this.api<{ pid: number }>("POST", "/launch", {
      exePath: game.exe_path,
      fullscreen: true,
      debugMetal: false,
      verbose: false,
      customArgs: [],
      launchMode: this.launchMode(),
    });

    if (result && typeof result === "object" && "pid" in result) {
      this.runningPid = (result as { pid: number }).pid;
      this.runningGameId = game.id;
      this.renderLibrary();
    }
  }

  private async killGame(game: Game) {
    if (!this.runningPid) return;
    await this.api("POST", "/kill", { pid: this.runningPid });
    this.runningPid = null;
    this.runningGameId = null;
    this.toast(`Stopped ${game.name}`);
    this.renderLibrary();
  }

  private renderSettings() {
    const el = document.getElementById("view-settings")!;
    const steam = this.steam;
    const cfg = this.config;
    const loginState = steam?.login_state;
    const loginBadge = loginState?.state === "logged_in"
      ? `<span class="badge badge-ok">Logged in (${loginState.account?.[0]?.name ?? "unknown"})</span>`
      : loginState?.state === "logged_out"
        ? `<span class="badge badge-warn">Logged out</span>`
        : `<span class="badge badge-warn">Unknown</span>`;

    const nativeLabel = cfg?.native_available ? "Available" : "Not Built";
    const wineLabel = cfg?.wine_available ? "Available" : "Not Found";

    el.innerHTML = `
      <div class="library-header">
        <h1>Settings</h1>
      </div>

      <div class="settings-section">
        <h2>Launch Mode</h2>
        <div class="settings-row">
          <div>
            <div class="settings-label">PE Loader (Native)</div>
            <div class="settings-desc">Run Windows executables directly through MetalSharp's PE loader — no Wine needed</div>
          </div>
          <div class="settings-value">
            <span class="badge ${cfg?.native_available ? "badge-ok" : "badge-warn"}">${nativeLabel}</span>
          </div>
        </div>
        <div class="settings-row">
          <div>
            <div class="settings-label">Wine Compatibility</div>
            <div class="settings-desc">Fall back to Wine for executables not yet supported natively</div>
          </div>
          <div class="settings-value">
            <span class="badge ${cfg?.wine_available ? "badge-ok" : "badge-warn"}">${wineLabel}</span>
          </div>
        </div>
        <div class="settings-row">
          <div>
            <div class="settings-label">Active Mode</div>
            <div class="settings-desc">Choose how games are launched</div>
          </div>
          <div class="settings-value">
            <select id="launch-mode-select" style="background:var(--bg-card);color:var(--text-primary);border:1px solid var(--border);border-radius:var(--radius-sm);padding:6px 12px;font-size:13px;">
              <option value="native" ${cfg?.launch_mode === "native" ? "selected" : ""}>PE Loader (Native)</option>
              <option value="wine" ${cfg?.launch_mode === "wine" ? "selected" : ""}>Wine</option>
            </select>
          </div>
        </div>
      </div>

      <div class="settings-section">
        <h2>Steam (Windows)</h2>
        <div class="settings-row">
          <div>
            <div class="settings-label">Windows Steam</div>
            <div class="settings-desc">Required for running games through MetalSharp</div>
          </div>
          <div class="settings-value">
            ${steam?.installed ? `<span class="badge badge-ok">Installed</span>` : `<span class="badge badge-warn">Not Installed</span>`}
          </div>
        </div>
        <div class="settings-row">
          <div>
            <div class="settings-label">Login State</div>
            <div class="settings-desc">Steam account login status in Wine prefix</div>
          </div>
          <div class="settings-value">${loginBadge}</div>
        </div>
        <div class="settings-row">
          <div>
            <div class="settings-label">Install Path</div>
            <div class="settings-desc">Windows Steam in Wine prefix</div>
          </div>
          <div class="settings-value">${steam?.path || "—"}</div>
        </div>
        ${!steam?.installed ? `
        <div style="padding-top:12px">
          <button class="btn btn-primary" id="btn-install-steam">Install Windows Steam</button>
        </div>` : ""}
      </div>

      <div class="settings-section">
        <h2>Steam (macOS)</h2>
        <div class="settings-row">
          <div>
            <div class="settings-label">macOS Steam</div>
            <div class="settings-desc">Native Mac client — not used by MetalSharp</div>
          </div>
          <div class="settings-value">
            ${steam?.mac_installed ? `<span class="badge badge-ok">Detected</span>` : `<span class="badge badge-warn">Not Found</span>`}
          </div>
        </div>
      </div>

      <div class="settings-section">
        <h2>SteamCMD</h2>
        <div class="settings-row">
          <div>
            <div class="settings-label">SteamCMD Path</div>
            <div class="settings-desc">Used for downloading Windows game depots</div>
          </div>
          <div class="settings-value">${steam?.steam_cmd_path || "Not found"}</div>
        </div>
      </div>

      <div class="settings-section">
        <h2>Launch Defaults</h2>
        <div class="settings-row">
          <div>
            <div class="settings-label">Fullscreen</div>
            <div class="settings-desc">Launch games in fullscreen mode</div>
          </div>
          <div class="settings-value">Enabled</div>
        </div>
        <div class="settings-row">
          <div>
            <div class="settings-label">Metal Debug</div>
            <div class="settings-desc">Enable Metal API validation layer</div>
          </div>
          <div class="settings-value">Disabled</div>
        </div>
      </div>
    `;

    el.querySelector("#btn-install-steam")?.addEventListener("click", async () => {
      this.toast("Installing Steam...");
      await this.api("POST", "/steam/install");
      await this.scan();
      this.renderSettings();
    });

    el.querySelector("#launch-mode-select")?.addEventListener("change", async (e) => {
      const mode = (e.target as HTMLSelectElement).value;
      await this.api("POST", "/config", { launchMode: mode });
      await this.loadConfig();
      this.toast(`Launch mode set to ${mode === "native" ? "PE Loader" : "Wine"}`);
    });
  }

  private renderStore() {
    const el = document.getElementById("view-store")!;

    el.innerHTML = `
      <div class="store-header">
        <h1>Download Games</h1>
        <p>Enter a Steam App ID to download via SteamCMD</p>
      </div>
      <div class="download-form">
        <input type="text" id="store-appid" placeholder="Steam App ID (e.g. 440 for TF2)" />
        <button class="btn btn-primary" id="btn-download">Download</button>
      </div>
      <div id="download-status" style="text-align:center;padding:24px;color:var(--text-dim);font-size:13px;"></div>
    `;

    el.querySelector("#btn-download")?.addEventListener("click", async () => {
      const input = el.querySelector("#store-appid") as HTMLInputElement;
      const appid = parseInt(input.value, 10);
      if (isNaN(appid)) {
        this.toast("Enter a valid Steam App ID", "error");
        return;
      }

      const status = el.querySelector("#download-status")!;
      status.innerHTML = `<div class="spinner"></div> Downloading...`;

      const result = await this.api<{ games: unknown[] }>("POST", "/steam/download-game", {
        steamAppId: appid,
      });

      if (result && typeof result === "object" && "games" in result) {
        const games = (result as { games: unknown[] }).games;
        status.innerHTML = `Downloaded ${games.length} executable(s). <a href="#" id="goto-library">View in Library</a>`;
        el.querySelector("#goto-library")?.addEventListener("click", (e) => {
          e.preventDefault();
          this.scan();
          this.switchView("library");
        });
      } else {
        status.innerHTML = `<span style="color:var(--error)">Download failed</span>`;
      }
    });
  }

  private toast(msg: string, type: "success" | "error" = "success") {
    const existing = document.querySelector(".toast");
    if (existing) existing.remove();

    const el = document.createElement("div");
    el.className = `toast ${type}`;
    el.textContent = msg;
    document.body.appendChild(el);
    setTimeout(() => el.remove(), 4000);
  }

  private formatBytes(bytes: number): string {
    if (bytes < 1024) return `${bytes} B`;
    if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
    if (bytes < 1024 * 1024 * 1024) return `${(bytes / (1024 * 1024)).toFixed(1)} MB`;
    return `${(bytes / (1024 * 1024 * 1024)).toFixed(1)} GB`;
  }

  private platformIcon(platform: string): string {
    switch (platform) {
      case "steam": return "\u2699";
      case "local": return "\uD83D\uDD33";
      default: return "\uD83C\uDFAE";
    }
  }

  private esc(s: string): string {
    const d = document.createElement("div");
    d.textContent = s;
    return d.innerHTML;
  }
}

document.addEventListener("DOMContentLoaded", () => {
  new App().init();
});
