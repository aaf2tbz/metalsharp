/// <reference path="./api-types.ts" />

function getAPI(): MetalsharpAPI {
  return (window as unknown as { metalsharp: MetalsharpAPI }).metalsharp;
}

interface SteamGame {
  appid: number;
  name: string;
  installed: boolean;
  state: "installed" | "not_installed" | "downloading";
  cover_url: string;
  header_url: string;
}

interface SteamLibrary {
  ok: boolean;
  total: number;
  installed_count: number;
  games: SteamGame[];
}

class App {
  private library: SteamLibrary | null = null;
  private steam: SteamStatus | null = null;
  private config: AppConfig | null = null;
  private runningPid: number | null = null;
  private runningAppId: number | null = null;
  private currentView = "library";
  private updateStatus: UpdateStatus | null = null;
  private downloadingAppId: number | null = null;
  private downloadProgress: number = 0;
  private progressInterval: ReturnType<typeof setInterval> | null = null;

  private steamApiKey: string | null = null;
  private steamcmdLoggedIn: boolean = false;

  async init() {
    this.bindNav();
    await this.checkBackend();
    await this.loadConfig();
    await this.checkForUpdates();
    this.steamApiKey = await this.getSteamApiKey();
    const cmdStatus = await this.api<{ logged_in: boolean }>("GET", "/steam/steamcmd-status");
    this.steamcmdLoggedIn = cmdStatus?.logged_in ?? false;
    await this.loadLibrary();
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
    if (view === "logs") this.renderLogs();
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

  private async checkForUpdates() {
    const result = await this.api<UpdateStatus>("GET", "/update/check");
    if (result) this.updateStatus = result;
  }

  private async loadLibrary() {
    const lib = await this.api<SteamLibrary>("GET", "/steam/library");
    if (lib) this.library = lib;

    const scan = await this.api<{ steam: SteamStatus }>("GET", "/scan");
    if (scan) this.steam = scan.steam ?? { installed: false, running: false };

    this.renderLibrary();
  }

  private async getSteamApiKey(): Promise<string | null> {
    const result = await this.api<{ key: string }>("GET", "/steam/api-key");
    return result?.key ?? null;
  }

  private launchMode(): string {
    return this.config?.launch_mode ?? "native";
  }

  private renderLibrary() {
    const el = document.getElementById("view-library")!;
    const lib = this.library;

    if (!lib || lib.games.length === 0) {
      el.innerHTML = `
        <div class="library-header">
          <div>
            <h1>Library</h1>
            <p class="subtitle">Loading your Steam library...</p>
          </div>
          <div class="header-actions">
            <button class="btn btn-secondary" id="btn-scan">Refresh</button>
          </div>
        </div>
        <div class="empty-state">
          <div class="empty-state-icon">&#x2699;</div>
          <h2>No games found</h2>
          <p>Could not load your Steam library. Check your Steam API key in settings.</p>
        </div>
      `;
      el.querySelector("#btn-scan")?.addEventListener("click", () => this.loadLibrary());
      return;
    }

    const installedGames = lib.games.filter(g => g.installed);
    const notInstalled = lib.games.filter(g => !g.installed);

    el.innerHTML = `
      <div class="library-header">
        <div>
          <h1>Library</h1>
          <p class="subtitle">${lib.total} games &middot; ${installedGames.length} installed</p>
        </div>
        <div class="header-actions">
          <input type="text" id="library-search" placeholder="Search games..." style="background:var(--bg-card);color:var(--text-primary);border:1px solid var(--border);border-radius:var(--radius-sm);padding:8px 14px;font-size:13px;width:220px;" />
          <select id="library-filter" style="background:var(--bg-card);color:var(--text-primary);border:1px solid var(--border);border-radius:var(--radius-sm);padding:8px 12px;font-size:13px;">
            <option value="all">All Games</option>
            <option value="installed">Installed</option>
            <option value="not_installed">Not Installed</option>
          </select>
          <button class="btn btn-secondary" id="btn-scan">Refresh</button>
        </div>
      </div>
      <div class="game-grid" id="game-grid"></div>
    `;

    this.renderGameGrid(lib.games);

    el.querySelector("#btn-scan")?.addEventListener("click", () => this.loadLibrary());
    el.querySelector("#library-search")?.addEventListener("input", () => this.filterGames());
    el.querySelector("#library-filter")?.addEventListener("change", () => this.filterGames());
  }

  private filterGames() {
    if (!this.library) return;
    const search = (document.getElementById("library-search") as HTMLInputElement)?.value.toLowerCase() ?? "";
    const filter = (document.getElementById("library-filter") as HTMLSelectElement)?.value ?? "all";

    let games = this.library.games;
    if (filter === "installed") games = games.filter(g => g.installed);
    if (filter === "not_installed") games = games.filter(g => !g.installed);
    if (search) games = games.filter(g => g.name.toLowerCase().includes(search));

    this.renderGameGrid(games);
  }

  private renderGameGrid(games: SteamGame[]) {
    const grid = document.getElementById("game-grid");
    if (!grid) return;
    grid.innerHTML = "";

    for (const game of games) {
      grid.appendChild(this.createGameCard(game));
    }
  }

  private createGameCard(game: SteamGame): HTMLElement {
    const card = document.createElement("div");
    card.className = "game-card";
    card.dataset.appid = String(game.appid);

    const isRunning = this.runningAppId === game.appid;
    const isDownloading = this.downloadingAppId === game.appid;
    if (isRunning) card.classList.add("running");

    const coverUrl = game.header_url || game.cover_url;
    const bannerContent = coverUrl
      ? `<img class="game-card-cover" src="${coverUrl}" alt="${this.esc(game.name)}" loading="lazy" onerror="this.onerror=null;this.src='';this.style.display='none';this.nextElementSibling.style.display='block'" /><span class="game-icon-placeholder" style="display:none">${this.esc(game.name).charAt(0).toUpperCase()}</span>`
      : `<span class="game-icon-placeholder">${this.esc(game.name).charAt(0).toUpperCase()}</span>`;

    let actionHtml: string;
    if (isDownloading) {
      actionHtml = `
        <div class="download-bar">
          <div class="download-progress" style="width:${this.downloadProgress}%"></div>
        </div>
        <span class="download-pct">${Math.round(this.downloadProgress)}%</span>
      `;
    } else if (isRunning) {
      actionHtml = `<button class="btn btn-stop" data-action="stop" data-appid="${game.appid}">Stop</button>`;
    } else if (game.installed) {
      actionHtml = `<button class="btn btn-play" data-action="play" data-appid="${game.appid}">Play</button>`;
    } else if (this.steamcmdLoggedIn) {
      actionHtml = `<button class="btn btn-install" data-action="install" data-appid="${game.appid}">Install</button>`;
    } else {
      actionHtml = `<span class="badge badge-warn">Login required</span>`;
    }

    card.innerHTML = `
      <div class="game-card-banner">
        ${bannerContent}
      </div>
      <div class="game-card-body">
        <div class="game-card-title">${this.esc(game.name)}</div>
        <div class="game-card-meta">
          ${game.installed ? `<span class="badge badge-ok">Installed</span>` : `<span class="badge badge-warn">Not Installed</span>`}
        </div>
        <div class="game-card-actions">
          ${actionHtml}
        </div>
      </div>
    `;

    card.querySelector("[data-action]")?.addEventListener("click", (e) => {
      const btn = e.currentTarget as HTMLElement;
      const action = btn.dataset.action;
      if (action === "play") this.launchGame(game);
      else if (action === "stop" && this.runningPid) this.stopGame(game);
      else if (action === "install") this.installGame(game);
    });

    return card;
  }

  private async installGame(game: SteamGame) {
    this.downloadingAppId = game.appid;
    this.downloadProgress = 0;

    const card = document.querySelector(`[data-appid="${game.appid}"]`);
    if (card) {
      const actions = card.querySelector(".game-card-actions");
      if (actions) {
        actions.innerHTML = `
          <div class="download-bar"><div class="download-progress" style="width:0%"></div></div>
          <span class="download-pct">0%</span>
        `;
      }
    }

    this.toast(`Installing ${game.name}...`, "success");

    const result = await this.api<{ ok: boolean }>("POST", "/steam/download-game", {
      steamAppId: game.appid,
    });

    if (!result?.ok) {
      this.toast(`Failed to start download`, "error");
      this.downloadingAppId = null;
      return;
    }

    if (this.progressInterval) clearInterval(this.progressInterval);
    const barEl = () => document.querySelector(`[data-appid="${game.appid}"] .download-progress`) as HTMLElement | null;
    const pctEl = () => document.querySelector(`[data-appid="${game.appid}"] .download-pct`);

    this.progressInterval = setInterval(async () => {
      try {
        const res = await getAPI().request("GET", "/steam/download-progress");
        const prog = (res?.data ?? res) as { progress?: number; status?: string } | null;
        if (!prog || typeof prog.progress !== "number") return;

        const bar = barEl();
        const pct = pctEl();
        if (bar) bar.style.width = `${prog.progress}%`;
        if (pct) pct.textContent = `${Math.round(prog.progress)}%`;

        if (prog.status === "complete") {
          if (this.progressInterval) { clearInterval(this.progressInterval); this.progressInterval = null; }
          this.downloadingAppId = null;
          this.toast(`${game.name} installed!`, "success");
          await this.loadLibrary();
        } else if (prog.status === "error") {
          if (this.progressInterval) { clearInterval(this.progressInterval); this.progressInterval = null; }
          this.downloadingAppId = null;
          this.toast(`Download failed`, "error");
        }
      } catch { }
    }, 2000);
  }

  private async launchGame(game: SteamGame) {
    this.toast(`Launching ${game.name}...`, "success");

    const result = await this.api<{ pid: number }>("POST", "/launch", {
      exePath: `~/.metalsharp/games/${game.appid}`,
      steamAppId: game.appid,
      fullscreen: true,
      debugMetal: false,
      verbose: false,
      customArgs: [],
      launchMode: this.launchMode(),
    });

    if (result && typeof result === "object" && "pid" in result) {
      this.runningPid = (result as { pid: number }).pid;
      this.runningAppId = game.appid;
      this.renderLibrary();
    }
  }

  private async stopGame(game: SteamGame) {
    if (!this.runningPid) return;
    await this.api("POST", "/kill", { pid: this.runningPid });
    this.runningPid = null;
    this.runningAppId = null;
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

    const savedKey = this.steamApiKey;
    const nativeLabel = cfg?.native_available ? "Available" : "Not Built";
    const wineLabel = cfg?.wine_available ? "Available" : "Not Found";

    el.innerHTML = `
      <div class="library-header">
        <h1>Settings</h1>
      </div>

      <div class="settings-section">
        <h2>Steam Integration</h2>
        <div class="settings-row">
          <div>
            <div class="settings-label">Steam Web API Key</div>
            <div class="settings-desc">Required to load your full game library (owned + uninstalled games). Get a free key at <a href="https://steamcommunity.com/dev/apikey" target="_blank" style="color:var(--orange)">steamcommunity.com/dev/apikey</a> — log in, fill in any domain name, and copy the key.</div>
          </div>
          <div class="settings-value">
            <div style="display:flex;gap:8px;align-items:center;">
              <input type="password" id="steam-api-key" value="${savedKey ?? ""}" placeholder="Enter your Steam Web API key..." style="background:var(--bg-card);color:var(--text-primary);border:1px solid var(--border);border-radius:var(--radius-sm);padding:6px 12px;font-size:13px;width:280px;" />
              <button class="btn btn-primary btn-sm" id="btn-save-api-key">Save & Sync</button>
            </div>
            ${savedKey ? `<span class="badge badge-ok" style="margin-top:6px;">Key saved — ${this.library?.total ?? 0} games synced</span>` : `<span class="badge badge-warn" style="margin-top:6px;">No key — only installed games shown</span>`}
          </div>
        </div>
        <div class="settings-row">
          <div>
            <div class="settings-label">SteamCMD Login</div>
            <div class="settings-desc">Required to download Windows game files. Uses your Steam username and password — credentials are sent only to Steam via SteamCMD and never stored.</div>
          </div>
          <div class="settings-value">
            <div id="steamcmd-login-area">
              <div style="display:flex;gap:8px;align-items:center;flex-wrap:wrap;">
                <input type="text" id="steam-username" placeholder="Steam username" style="background:var(--bg-card);color:var(--text-primary);border:1px solid var(--border);border-radius:var(--radius-sm);padding:6px 12px;font-size:13px;width:180px;" />
                <input type="password" id="steam-password" placeholder="Steam password" style="background:var(--bg-card);color:var(--text-primary);border:1px solid var(--border);border-radius:var(--radius-sm);padding:6px 12px;font-size:13px;width:180px;" />
                <button class="btn btn-primary btn-sm" id="btn-steamcmd-login">Login</button>
              </div>
              <div id="steamcmd-status" style="margin-top:6px;"></div>
            </div>
          </div>
        </div>
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
        <h2>Graphics</h2>
        <div class="settings-row">
          <div>
            <div class="settings-label">Resolution</div>
            <div class="settings-desc">Internal render resolution</div>
          </div>
          <div class="settings-value">
            <select id="resolution-select" style="background:var(--bg-card);color:var(--text-primary);border:1px solid var(--border);border-radius:var(--radius-sm);padding:6px 12px;font-size:13px;">
              <option value="1280x720">1280 x 720</option>
              <option value="1920x1080" selected>1920 x 1080</option>
              <option value="2560x1440">2560 x 1440</option>
              <option value="3840x2160">3840 x 2160</option>
            </select>
          </div>
        </div>
        <div class="settings-row">
          <div>
            <div class="settings-label">Window Mode</div>
            <div class="settings-desc">Fullscreen, borderless, or windowed</div>
          </div>
          <div class="settings-value">
            <select id="window-mode-select" style="background:var(--bg-card);color:var(--text-primary);border:1px solid var(--border);border-radius:var(--radius-sm);padding:6px 12px;font-size:13px;">
              <option value="fullscreen" selected>Fullscreen</option>
              <option value="borderless">Borderless</option>
              <option value="windowed">Windowed</option>
            </select>
          </div>
        </div>
        <div class="settings-row">
          <div>
            <div class="settings-label">Upscaling</div>
            <div class="settings-desc">MetalFX spatial upscaling quality</div>
          </div>
          <div class="settings-value">
            <select id="upscaling-select" style="background:var(--bg-card);color:var(--text-primary);border:1px solid var(--border);border-radius:var(--radius-sm);padding:6px 12px;font-size:13px;">
              <option value="off" selected>Off</option>
              <option value="low">Low</option>
              <option value="medium">Medium</option>
              <option value="high">High</option>
              <option value="ultra">Ultra</option>
            </select>
          </div>
        </div>
        <div class="settings-row">
          <div>
            <div class="settings-label">VSync</div>
            <div class="settings-desc">Synchronize frames to display refresh rate</div>
          </div>
          <div class="settings-value">Enabled</div>
        </div>
      </div>

      <div class="settings-section">
        <h2>Shader Cache</h2>
        <div class="settings-row">
          <div>
            <div class="settings-label">Shader Cache</div>
            <div class="settings-desc">Persist compiled shaders to disk for faster loading</div>
          </div>
          <div class="settings-value">
            <button class="btn btn-secondary btn-sm" id="btn-clear-shader-cache">Clear Cache</button>
          </div>
        </div>
        <div class="settings-row">
          <div>
            <div class="settings-label">Pipeline Cache</div>
            <div class="settings-desc">Persist compiled pipeline state objects</div>
          </div>
          <div class="settings-value">
            <button class="btn btn-secondary btn-sm" id="btn-clear-pipeline-cache">Clear Cache</button>
          </div>
        </div>
      </div>

      <div class="settings-section">
        <h2>Crash Reports</h2>
        <div class="settings-row">
          <div>
            <div class="settings-label">Automatic Crash Reporting</div>
            <div class="settings-desc">Collect logs and diagnostics when games crash</div>
          </div>
          <div class="settings-value">
            <button class="btn btn-secondary btn-sm" id="btn-view-crashes">View Reports</button>
          </div>
        </div>
      </div>

      <div class="settings-section">
        <h2>Updates</h2>
        <div class="settings-row">
          <div>
            <div class="settings-label">Auto-Check Updates</div>
            <div class="settings-desc">Check for new MetalSharp versions on startup</div>
          </div>
          <div class="settings-value">Enabled</div>
        </div>
        ${this.updateStatus?.available ? `
        <div class="settings-row">
          <div>
            <div class="settings-label">Update Available</div>
            <div class="settings-desc">v${this.esc(this.updateStatus.latest_version)} (current: v${this.esc(this.updateStatus.current_version)})</div>
          </div>
          <div class="settings-value">
            <a href="${this.updateStatus.download_url}" target="_blank" class="btn btn-primary btn-sm">Download</a>
          </div>
        </div>
        ` : `
        <div class="settings-row">
          <div>
            <div class="settings-label">Version</div>
            <div class="settings-desc">You're up to date</div>
          </div>
          <div class="settings-value">
            <span class="badge badge-ok">v${this.updateStatus?.current_version ?? "0.1.0"}</span>
          </div>
        </div>
        `}
      </div>

      <div class="settings-section">
        <h2>Launch Defaults</h2>
        <div class="settings-row">
          <div>
            <div class="settings-label">Metal Debug</div>
            <div class="settings-desc">Enable Metal API validation layer</div>
          </div>
          <div class="settings-value">Disabled</div>
        </div>
      </div>
    `;

    el.querySelector("#btn-save-api-key")?.addEventListener("click", async () => {
      const input = document.getElementById("steam-api-key") as HTMLInputElement;
      const key = input?.value?.trim();
      if (!key) {
        this.toast("Please enter a Steam API key", "error");
        return;
      }
      await this.api("POST", "/steam/save-api-key", { key });
      this.toast("API key saved — syncing library...", "success");
      await this.loadLibrary();
      this.renderSettings();
    });

    const initSteamcmdStatus = async () => {
      const status = await this.api<{ logged_in: boolean; username?: string }>("GET", "/steam/steamcmd-status");
      const area = document.getElementById("steamcmd-login-area");
      if (!area) return;
      if (status?.logged_in && status.username) {
        area.innerHTML = `
          <div style="display:flex;gap:8px;align-items:center;">
            <span class="badge badge-ok">Logged in as ${this.esc(status.username)}</span>
            <button class="btn btn-secondary btn-sm" id="btn-steamcmd-logout">Logout</button>
          </div>
        `;
        area.querySelector("#btn-steamcmd-logout")?.addEventListener("click", async () => {
          await this.api("POST", "/steam/steamcmd-logout");
          this.toast("Logged out of SteamCMD");
          this.renderSettings();
        });
      }
    };
    initSteamcmdStatus();

    el.querySelector("#btn-steamcmd-login")?.addEventListener("click", async () => {
      const username = (document.getElementById("steam-username") as HTMLInputElement)?.value?.trim();
      const password = (document.getElementById("steam-password") as HTMLInputElement)?.value;
      if (!username || !password) {
        this.toast("Enter your Steam username and password", "error");
        return;
      }
      const btn = document.getElementById("btn-steamcmd-login") as HTMLElement;
      if (btn) btn.textContent = "Logging in...";
      const result = await this.api<{ ok: boolean; error?: string }>("POST", "/steam/steamcmd-login", { username, password });
      if (result?.ok) {
        this.steamcmdLoggedIn = true;
        this.toast("SteamCMD login successful!", "success");
        this.renderSettings();
        this.renderLibrary();
      } else {
        this.toast(result?.error ?? "Login failed", "error");
        if (btn) btn.textContent = "Login";
      }
    });

    el.querySelector("#btn-install-steam")?.addEventListener("click", async () => {
      this.toast("Installing Steam...");
      await this.api("POST", "/steam/install");
      await this.loadLibrary();
      this.renderSettings();
    });

    el.querySelector("#launch-mode-select")?.addEventListener("change", async (e) => {
      const mode = (e.target as HTMLSelectElement).value;
      await this.api("POST", "/config", { launchMode: mode });
      await this.loadConfig();
      this.toast(`Launch mode set to ${mode === "native" ? "PE Loader" : "Wine"}`);
    });

    el.querySelector("#btn-clear-shader-cache")?.addEventListener("click", async () => {
      await this.api("POST", "/cache/clear", { type: "shader" });
      this.toast("Shader cache cleared");
    });

    el.querySelector("#btn-clear-pipeline-cache")?.addEventListener("click", async () => {
      await this.api("POST", "/cache/clear", { type: "pipeline" });
      this.toast("Pipeline cache cleared");
    });

    el.querySelector("#btn-view-crashes")?.addEventListener("click", async () => {
      const reports = await this.api<CrashReportSummary[]>("GET", "/crash-reports");
      if (reports && Array.isArray(reports) && reports.length > 0) {
        const lines = reports.map((r: CrashReportSummary) => `[${r.timestamp}] ${r.game} (exit ${r.exit_code})`).join("\n");
        this.toast(`${reports.length} crash report(s) found. See Logs.`, "success");
      } else {
        this.toast("No crash reports found.");
      }
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

      const progressInterval = setInterval(async () => {
        const prog = await this.api<{ progress: number | null; line?: string }>("GET", "/steam/download-progress");
        if (prog && prog.progress != null) {
          status.innerHTML = `<div class="spinner"></div> Downloading... ${prog.progress.toFixed(1)}%`;
        }
      }, 2000);

      try {
        const result = await this.api<{ games: unknown[] }>("POST", "/steam/download-game", {
          steamAppId: appid,
        });

        clearInterval(progressInterval);

        if (result && typeof result === "object" && "games" in result) {
          const games = (result as { games: unknown[] }).games;
          status.innerHTML = `Downloaded ${games.length} executable(s). <a href="#" id="goto-library">View in Library</a>`;
          el.querySelector("#goto-library")?.addEventListener("click", (e) => {
            e.preventDefault();
            this.loadLibrary();
            this.switchView("library");
          });
        } else {
          this.showError("Download Failed", "The download did not complete successfully. Check the Logs tab for details.");
          status.innerHTML = `<span style="color:var(--error)">Download failed</span>`;
        }
      } catch (e) {
        clearInterval(progressInterval);
        this.showError("Download Error", (e as Error).message);
      }
    });
  }

  private async renderLogs() {
    const el = document.getElementById("view-logs")!;
    el.innerHTML = `
      <div class="library-header">
        <div>
          <h1>Logs</h1>
          <p class="subtitle">MetalSharp runtime logs</p>
        </div>
        <div class="header-actions">
          <button class="btn btn-secondary" id="btn-refresh-logs">Refresh</button>
        </div>
      </div>
      <div id="log-content" style="background:var(--bg-deep);border:1px solid var(--border);border-radius:var(--radius-md);padding:16px;font-family:'SF Mono',Menlo,monospace;font-size:12px;line-height:1.6;max-height:calc(100vh - 200px);overflow-y:auto;color:var(--text-secondary);white-space:pre-wrap;"></div>
    `;

    el.querySelector("#btn-refresh-logs")?.addEventListener("click", () => this.renderLogs());

    const result = await this.api<{ logs: { name: string; lines: string[] }[] }>("GET", "/logs");
    const content = el.querySelector("#log-content")!;
    if (result && result.logs && result.logs.length > 0) {
      const latest = result.logs[result.logs.length - 1];
      content.textContent = latest.lines.join("\n");
    } else {
      content.textContent = "No logs found. Logs are written to ~/.metalsharp/logs/ during game execution.";
    }
  }

  private showError(title: string, message: string) {
    const existing = document.querySelector(".error-dialog");
    if (existing) existing.remove();

    const overlay = document.createElement("div");
    overlay.className = "error-dialog";
    overlay.innerHTML = `
      <div class="error-dialog-backdrop"></div>
      <div class="error-dialog-content">
        <h3>${this.esc(title)}</h3>
        <p>${this.esc(message)}</p>
        <button class="btn btn-secondary" id="error-dismiss">Dismiss</button>
      </div>
    `;
    document.body.appendChild(overlay);
    overlay.querySelector("#error-dismiss")?.addEventListener("click", () => overlay.remove());
    overlay.querySelector(".error-dialog-backdrop")?.addEventListener("click", () => overlay.remove());
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
