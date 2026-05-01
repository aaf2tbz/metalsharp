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
  private setupState: SetupState | null = null;
  private setupStep = 0;
  private setupDeviceName = "";

  async init() {
    this.bindNav();
    await this.checkBackend();

    const firstLaunch = await getAPI().isFirstLaunch();
    if (firstLaunch) {
      this.showSetupWizard();
      return;
    }

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

  // === SETUP WIZARD ===

  private showSetupWizard() {
    const overlay = document.createElement("div");
    overlay.className = "setup-overlay";
    overlay.id = "setup-overlay";
    document.body.appendChild(overlay);

    this.renderSetupStep(0);
  }

  private hideSetupWizard() {
    const overlay = document.getElementById("setup-overlay");
    if (overlay) overlay.remove();
  }

  private async renderSetupStep(step: number) {
    this.setupStep = step;
    const overlay = document.getElementById("setup-overlay")!;
    overlay.innerHTML = "";

    const wizard = document.createElement("div");
    wizard.className = "setup-wizard";
    overlay.appendChild(wizard);

    switch (step) {
      case 0: this.renderSetupWelcome(wizard); break;
      case 1: this.renderSetupDependencies(wizard); break;
      case 2: this.renderSetupDeviceName(wizard); break;
      case 3: this.renderSetupSteamApiKey(wizard); break;
      case 4: this.renderSetupSteamLogin(wizard); break;
      case 5: this.renderSetupComplete(wizard); break;
    }
  }

  private renderSetupStepIndicator(container: HTMLElement, current: number) {
    const steps = ["Welcome", "Dependencies", "Device", "Steam API", "Steam Login", "Done"];
    const indicator = document.createElement("div");
    indicator.className = "setup-steps";

    for (let i = 0; i < steps.length; i++) {
      const dot = document.createElement("div");
      dot.className = "setup-step-dot";
      if (i < current) dot.classList.add("done");
      if (i === current) dot.classList.add("current");

      const label = document.createElement("span");
      label.className = "setup-step-label";
      label.textContent = steps[i];
      if (i === current) label.classList.add("current");

      const wrapper = document.createElement("div");
      wrapper.className = "setup-step-item";
      wrapper.appendChild(dot);
      wrapper.appendChild(label);
      indicator.appendChild(wrapper);

      if (i < steps.length - 1) {
        const line = document.createElement("div");
        line.className = "setup-step-line";
        if (i < current) line.classList.add("done");
        indicator.appendChild(line);
      }
    }

    container.appendChild(indicator);
  }

  private renderSetupWelcome(container: HTMLElement) {
    container.innerHTML = `
      <div class="setup-hero">
        <div class="setup-hero-icon">M</div>
        <h1 class="setup-hero-title">Welcome to MetalSharp</h1>
        <p class="setup-hero-sub">Run Windows games natively on macOS with Metal. No VM, no Vulkan.</p>
      </div>
      <div class="setup-body">
        <div class="setup-features">
          <div class="setup-feature">
            <div class="setup-feature-icon">&#9889;</div>
            <div>
              <div class="setup-feature-title">Direct3D to Metal</div>
              <div class="setup-feature-desc">Single-hop translation — faster than DXVK+MoltenVK</div>
            </div>
          </div>
          <div class="setup-feature">
            <div class="setup-feature-icon">&#127918;</div>
            <div>
              <div class="setup-feature-title">XNA/FNA Native</div>
              <div class="setup-feature-desc">Run Celeste, Terraria, and other XNA games via FNA + SDL3 + Metal</div>
            </div>
          </div>
          <div class="setup-feature">
            <div class="setup-feature-icon">&#128274;</div>
            <div>
              <div class="setup-feature-title">Steam Integration</div>
              <div class="setup-feature-desc">Browse your library, download games, launch with one click</div>
            </div>
          </div>
        </div>
        <div class="setup-actions">
          <button class="btn btn-primary btn-lg" id="setup-next">Get Started</button>
        </div>
      </div>
    `;
    this.renderSetupStepIndicator(container, 0);

    container.querySelector("#setup-next")?.addEventListener("click", () => {
      this.renderSetupStep(1);
    });
  }

  private async renderSetupDependencies(container: HTMLElement) {
    this.renderSetupStepIndicator(container, 1);

    const body = document.createElement("div");
    body.className = "setup-body";
    container.appendChild(body);

    body.innerHTML = `
      <div class="setup-section-header">
        <h1>Install Dependencies</h1>
        <p>MetalSharp needs a few system libraries to run games.</p>
      </div>
      <div id="dep-list" class="setup-dep-list"></div>
      <div class="setup-actions">
        <button class="btn btn-secondary" id="setup-back">Back</button>
        <button class="btn btn-primary" id="setup-install-deps">Install Missing</button>
        <button class="btn btn-secondary" id="setup-skip">Skip</button>
      </div>
    `;

    const depList = body.querySelector("#dep-list")!;
    const deps = await this.api<DependenciesResponse>("GET", "/setup/dependencies");

    if (deps?.dependencies) {
      for (const dep of deps.dependencies) {
        const row = document.createElement("div");
        row.className = `setup-dep-row ${dep.installed ? "installed" : ""}`;
        row.innerHTML = `
          <div class="setup-dep-status">${dep.installed ? "&#10003;" : "&#10007;"}</div>
          <div class="setup-dep-info">
            <div class="setup-dep-name">${this.esc(dep.name)}${dep.required ? ' <span class="badge badge-warn">Required</span>' : ""}</div>
            <div class="setup-dep-desc">${this.esc(dep.desc)}</div>
          </div>
          <div class="setup-dep-badge">${dep.installed ? '<span class="badge badge-ok">Installed</span>' : '<span class="badge badge-warn">Missing</span>'}</div>
        `;
        depList.appendChild(row);
      }
    }

    body.querySelector("#setup-back")?.addEventListener("click", () => this.renderSetupStep(0));

    body.querySelector("#setup-install-deps")?.addEventListener("click", async () => {
      const btn = body.querySelector("#setup-install-deps") as HTMLElement;
      const backBtn = body.querySelector("#setup-back") as HTMLElement;
      const skipBtn = body.querySelector("#setup-skip") as HTMLElement;
      const allBtns = [btn, backBtn, skipBtn].filter(Boolean) as HTMLElement[];
      allBtns.forEach((b) => (b as HTMLButtonElement).disabled = true);
      btn.textContent = "Checking...";

      const brewDep = deps?.dependencies?.find((d) => d.id === "homebrew");
      if (brewDep && !brewDep.installed) {
        this.toast("Homebrew is required to install dependencies. Install it from https://brew.sh, then restart MetalSharp.", "error");
        allBtns.forEach((b) => (b as HTMLButtonElement).disabled = false);
        btn.textContent = "Install Missing";
        return;
      }

      const missing = deps?.dependencies?.filter((d) => !d.installed && d.required) ?? [];
      if (missing.length === 0) {
        this.toast("All required dependencies are already installed.", "success");
        allBtns.forEach((b) => (b as HTMLButtonElement).disabled = false);
        this.renderSetupStep(2);
        return;
      }

      const brewable = missing.filter((d) => d.installCmd?.startsWith("brew "));
      let anyFailed = false;

      for (const dep of brewable) {
        btn.textContent = `Installing ${dep.name}...`;
        const result = await getAPI().installDeps(dep.installCmd);
        if (!result.ok) {
          anyFailed = true;
          this.toast(`Failed to install ${dep.name}: ${result.error}`, "error");
        } else {
          this.toast(`${dep.name} installed successfully`, "success");
        }
      }

      btn.textContent = "Verifying...";
      const verify = await this.api<DependenciesResponse>("GET", "/setup/dependencies");
      const stillMissing = verify?.dependencies?.filter((d) => !d.installed && d.required) ?? [];

      if (anyFailed || stillMissing.length > 0) {
        const names = stillMissing.map((d) => d.name).join(", ");
        this.toast(`Some dependencies could not be installed: ${names}. You can retry or skip.`, "error");
        btn.textContent = "Retry Install";
        allBtns.forEach((b) => (b as HTMLButtonElement).disabled = false);
        this.renderSetupDependencies(container);
        return;
      }

      this.toast("All dependencies installed and verified!", "success");
      this.renderSetupStep(2);
    });

    body.querySelector("#setup-skip")?.addEventListener("click", () => this.renderSetupStep(2));
  }

  private async renderSetupDeviceName(container: HTMLElement) {
    this.renderSetupStepIndicator(container, 2);

    const body = document.createElement("div");
    body.className = "setup-body";
    container.appendChild(body);

    const defaultName = this.setupDeviceName || "";
    body.innerHTML = `
      <div class="setup-body">
        <div class="setup-section-header">
          <h1>Device Name</h1>
          <p>Choose a name for this device. This helps Steam recognize your machine so you don't need to re-login every session.</p>
        </div>
        <div class="setup-form">
          <div class="setup-form-group">
            <label class="setup-label">Device Name</label>
            <input type="text" id="device-name-input" value="${this.esc(defaultName)}" placeholder="e.g. Swift-Falcon" class="setup-input" />
            <div class="setup-hint">This is stored locally and sent to Steam as your machine identifier.</div>
          </div>
          <div class="setup-form-group">
            <button class="btn btn-secondary btn-sm" id="regen-name">Generate Random Name</button>
          </div>
        </div>
        <div class="setup-actions">
          <button class="btn btn-secondary" id="setup-back">Back</button>
          <button class="btn btn-primary" id="setup-next">Continue</button>
        </div>
      </div>
    `;

    if (!defaultName) {
      this.api<{ name: string }>("GET", "/setup/device-name").then((result) => {
        const input = document.getElementById("device-name-input") as HTMLInputElement;
        if (result?.name && input && !input.value) {
          input.value = result.name;
          this.setupDeviceName = result.name;
        }
      });
    }

    container.querySelector("#regen-name")?.addEventListener("click", async () => {
      const result = await this.api<{ name: string }>("GET", "/setup/device-name");
      const input = document.getElementById("device-name-input") as HTMLInputElement;
      if (result?.name && input) input.value = result.name;
    });

    container.querySelector("#setup-back")?.addEventListener("click", () => this.renderSetupStep(1));

    container.querySelector("#setup-next")?.addEventListener("click", async () => {
      const input = document.getElementById("device-name-input") as HTMLInputElement;
      this.setupDeviceName = input?.value?.trim() || this.setupDeviceName;

      await this.api("POST", "/setup/save", {
        step: 2,
        deviceName: this.setupDeviceName,
      });

      this.renderSetupStep(3);
    });
  }

  private renderSetupSteamApiKey(container: HTMLElement) {
    this.renderSetupStepIndicator(container, 3);

    const body = document.createElement("div");
    body.className = "setup-body";
    container.appendChild(body);

    body.innerHTML = `
      <div class="setup-body">
        <div class="setup-section-header">
          <h1>Steam Web API Key</h1>
          <p>Required to load your full game library (including uninstalled games).</p>
        </div>
        <div class="setup-form">
          <div class="setup-form-group">
            <label class="setup-label">API Key</label>
            <input type="password" id="setup-api-key" placeholder="Enter your Steam Web API key..." class="setup-input" />
            <div class="setup-hint">
              Get a free key at <a href="https://steamcommunity.com/dev/apikey" target="_blank" style="color:var(--orange)">steamcommunity.com/dev/apikey</a> — 
              log in with Steam, fill in any domain name, and copy the key.
            </div>
          </div>
        </div>
        <div class="setup-actions">
          <button class="btn btn-secondary" id="setup-back">Back</button>
          <button class="btn btn-secondary" id="setup-skip">Skip for Now</button>
          <button class="btn btn-primary" id="setup-next">Save & Continue</button>
        </div>
      </div>
    `;

    body.querySelector("#setup-back")?.addEventListener("click", () => this.renderSetupStep(2));

    body.querySelector("#setup-skip")?.addEventListener("click", () => this.renderSetupStep(4));

    body.querySelector("#setup-next")?.addEventListener("click", async () => {
      const input = document.getElementById("setup-api-key") as HTMLInputElement;
      const key = input?.value?.trim();
      if (!key) {
        this.toast("Enter your API key or skip", "error");
        return;
      }

      await this.api("POST", "/steam/save-api-key", { key });
      await this.api("POST", "/setup/save", { step: 3, steamApiKeySet: true });
      this.steamApiKey = key;

      this.toast("API key saved!", "success");
      this.renderSetupStep(4);
    });
  }

  private renderSetupSteamLogin(container: HTMLElement) {
    this.renderSetupStepIndicator(container, 4);

    const body = document.createElement("div");
    body.className = "setup-body";
    container.appendChild(body);

    body.innerHTML = `
      <div class="setup-body">
        <div class="setup-section-header">
          <h1>Steam Login</h1>
          <p>Log in with Steam to download and run games. Credentials are sent only to Steam — never stored.</p>
        </div>
        <div class="setup-form">
          <div class="setup-form-group">
            <label class="setup-label">Steam Username</label>
            <input type="text" id="setup-steam-user" placeholder="Your Steam username" class="setup-input" />
          </div>
          <div class="setup-form-group">
            <label class="setup-label">Steam Password</label>
            <input type="password" id="setup-steam-pass" placeholder="Your Steam password" class="setup-input" />
            <div class="setup-hint">Steam Guard will send a confirmation to your mobile app.</div>
          </div>
          <div id="setup-login-status"></div>
        </div>
        <div class="setup-actions">
          <button class="btn btn-secondary" id="setup-back">Back</button>
          <button class="btn btn-secondary" id="setup-skip">Skip for Now</button>
          <button class="btn btn-primary" id="setup-login">Login</button>
        </div>
      </div>
    `;

    body.querySelector("#setup-back")?.addEventListener("click", () => this.renderSetupStep(3));

    body.querySelector("#setup-skip")?.addEventListener("click", async () => {
      await this.api("POST", "/setup/save", { step: 4 });
      this.renderSetupStep(5);
    });

    body.querySelector("#setup-login")?.addEventListener("click", async () => {
      const username = (document.getElementById("setup-steam-user") as HTMLInputElement)?.value?.trim();
      const password = (document.getElementById("setup-steam-pass") as HTMLInputElement)?.value;
      const btn = body.querySelector("#setup-login") as HTMLElement;
      const status = document.getElementById("setup-login-status")!;

      if (!username || !password) {
        this.toast("Enter your Steam credentials", "error");
        return;
      }

      btn.textContent = "Logging in...";
      (btn as HTMLButtonElement).disabled = true;
      status.innerHTML = '<div class="spinner"></div> Connecting to Steam...';

      const result = await this.api<{ ok: boolean; error?: string }>("POST", "/steam/steamcmd-login", { username, password });

      if (result?.ok) {
        this.steamcmdLoggedIn = true;
        await this.api("POST", "/setup/save", { step: 4, steamcmdLoggedIn: true });
        status.innerHTML = '<span class="badge badge-ok">Connected!</span>';
        this.toast("Steam login successful!", "success");

        setTimeout(() => this.renderSetupStep(5), 800);
      } else {
        status.innerHTML = `<span style="color:var(--error)">${this.esc(result?.error ?? "Login failed")}</span>`;
        btn.textContent = "Login";
        (btn as HTMLButtonElement).disabled = false;
      }
    });
  }

  private async renderSetupComplete(container: HTMLElement) {
    this.renderSetupStepIndicator(container, 5);

    await this.api("POST", "/setup/save", { step: 5, completed: true });

    const body = document.createElement("div");
    body.className = "setup-body";
    container.appendChild(body);

    body.innerHTML = `
      <div class="setup-body">
        <div class="setup-complete">
          <div class="setup-complete-icon">&#10003;</div>
          <h1>You're All Set!</h1>
          <p>MetalSharp is ready to go. Download games from your library and play them natively on macOS.</p>
          <div class="setup-complete-tips">
            <div class="setup-tip">
              <strong>Download a game</strong> — Find it in your Library and click Install.
            </div>
            <div class="setup-tip">
              <strong>First launch</strong> — MetalSharp auto-configures the runtime (FNA, shims, etc.) for each game.
            </div>
            <div class="setup-tip">
              <strong>No audio?</strong> — Some games use FMOD which has no arm64 macOS build. Audio stubs are applied automatically.
            </div>
          </div>
          <div class="setup-actions" style="justify-content:center;margin-top:32px;">
            <button class="btn btn-primary btn-lg" id="setup-finish">Launch MetalSharp</button>
          </div>
        </div>
      </div>
    `;

    body.querySelector("#setup-finish")?.addEventListener("click", async () => {
      this.hideSetupWizard();

      this.steamApiKey = await this.getSteamApiKey();
      const cmdStatus = await this.api<{ logged_in: boolean }>("GET", "/steam/steamcmd-status");
      this.steamcmdLoggedIn = cmdStatus?.logged_in ?? false;
      await this.loadConfig();
      await this.loadLibrary();
    });
  }

  // === LIBRARY ===

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
      const statusText = this.downloadProgress >= 95 ? "Installing..." : `Downloading ${Math.round(this.downloadProgress)}%`;
      actionHtml = `
        <div class="download-bar">
          <div class="download-progress" style="width:${this.downloadProgress}%"></div>
        </div>
        <span class="download-pct">${statusText}</span>
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

        if (prog.status === "installing") {
          if (pct) pct.textContent = "Installing...";
        } else {
          if (pct) pct.textContent = `${Math.round(prog.progress)}%`;
        }

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
    this.toast(`Preparing ${game.name}...`, "success");

    const prep = await this.api<{ ok: boolean; gameType: string }>("POST", "/game/prepare", { appid: game.appid });

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
        <div class="settings-row">
          <div>
            <div class="settings-label">Device Name</div>
            <div class="settings-desc">Identifies this machine to Steam for persistent login</div>
          </div>
          <div class="settings-value">
            <div style="display:flex;gap:8px;align-items:center;">
              <span>${this.esc(this.setupDeviceName || "Not set")}</span>
              <button class="btn btn-secondary btn-sm" id="btn-change-device">Change</button>
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
        <h2>Steam</h2>
        <div class="settings-row">
          <div>
            <div class="settings-label">Steam</div>
            <div class="settings-desc">Used to download games via SteamCMD</div>
          </div>
          <div class="settings-value">
            ${steam?.installed ? `<span class="badge badge-ok">Installed</span>` : `<span class="badge badge-warn">Not Found</span>`}
          </div>
        </div>
        <div class="settings-row">
          <div>
            <div class="settings-label">Install Path</div>
            <div class="settings-desc">Steam installation directory</div>
          </div>
          <div class="settings-value">${steam?.path || "—"}</div>
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
        <input type="text" id="store-appid" placeholder="Steam App ID (e.g. 504230 for Celeste)" />
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
