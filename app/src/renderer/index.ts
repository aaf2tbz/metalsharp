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
  size_bytes?: number | null;
  launch_method?: string;
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
  private pollingForInstall: number | null = null;
  private libraryFilter: string = "all";
  private wineSteamInstalled: boolean = false;
  private wineSteamRunning: boolean = false;
  private metalsharpWineAvailable: boolean = false;
  private launchingAppId: number | null = null;
  private theme: "dark" | "light" = "dark";

  private steamApiKey: string | null = null;
  private setupState: SetupState | null = null;
  private setupStep = 0;
  private setupDeviceName = "";

  async init() {
    this.initTheme();
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
    const setupState = await this.api<{ deviceName?: string }>("GET", "/setup/state");
    if (setupState?.deviceName) this.setupDeviceName = setupState.deviceName;
    await this.loadLibrary();
  }

  private bindNav() {
    document.querySelectorAll(".nav-item").forEach((btn) => {
      btn.addEventListener("click", () => {
        const view = (btn as HTMLElement).dataset.view!;
        this.switchView(view);
      });
    });
    document.getElementById("btn-theme")?.addEventListener("click", () => this.toggleTheme());
  }

  private initTheme() {
    const saved = localStorage.getItem("metalsharp-theme");
    this.theme = saved === "light" ? "light" : "dark";
    document.documentElement.dataset.theme = this.theme;
    this.updateThemeToggle();
  }

  private toggleTheme() {
    this.theme = this.theme === "dark" ? "light" : "dark";
    localStorage.setItem("metalsharp-theme", this.theme);
    document.documentElement.dataset.theme = this.theme;
    this.updateThemeToggle();
  }

  private updateThemeToggle() {
    const btn = document.getElementById("btn-theme");
    if (!btn) return;
    const next = this.theme === "dark" ? "light" : "dark";
    btn.textContent = this.theme === "dark" ? "Light Mode" : "Dark Mode";
    btn.setAttribute("title", `Toggle ${next} mode`);
  }

  private switchView(view: string) {
    this.currentView = view;
    document.querySelectorAll(".nav-item").forEach((b) => b.classList.remove("active"));
    document.querySelector(`[data-view="${view}"]`)?.classList.add("active");
    document.querySelectorAll(".view").forEach((v) => v.classList.add("hidden"));
    document.getElementById(`view-${view}`)?.classList.remove("hidden");

    if (view === "settings") this.renderSettings();
    if (view === "logs") this.renderLogs();
  }

  private async api<T = unknown>(method: string, url: string, body?: Record<string, unknown>, timeoutMs?: number): Promise<T | null> {
    try {
      const res = await getAPI().request(method, url, body, timeoutMs);
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

    const steamStatus = await this.api<{ installed: boolean; running: boolean; metalsharp_wine_available: boolean }>("GET", "/steam/status");
    this.wineSteamInstalled = steamStatus?.installed ?? false;
    this.wineSteamRunning = steamStatus?.running ?? false;
    this.metalsharpWineAvailable = steamStatus?.metalsharp_wine_available ?? false;

    this.renderLibrary();
  }

  private async getSteamApiKey(): Promise<string | null> {
    const result = await this.api<{ key: string }>("GET", "/steam/api-key");
    return result?.key ?? null;
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
      case 1: this.renderSetupInstall(wizard); break;
      case 2: this.renderSetupSteam(wizard); break;
      case 3: this.renderSetupComplete(wizard); break;
    }
  }

  private renderSetupStepIndicator(container: HTMLElement, current: number) {
    const steps = ["Welcome", "Install", "Steam", "Done"];
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

  private async renderSetupInstall(container: HTMLElement) {
    this.renderSetupStepIndicator(container, 1);

    const body = document.createElement("div");
    body.className = "setup-body";
    container.appendChild(body);

    body.innerHTML = `
      <div class="setup-section-header">
        <h1>Install Dependencies</h1>
        <p>MetalSharp needs a few system tools and runtimes. Install Homebrew first, then hit the button below.</p>
      </div>
      <div id="install-buttons" class="setup-install-buttons"></div>
      <div id="install-log-container" class="setup-install-log-container" style="display:none;">
        <div class="setup-progress-bar-container">
          <div class="setup-progress-bar" id="install-progress-bar"></div>
          <span class="setup-progress-label" id="install-progress-label">0%</span>
        </div>
        <div class="setup-install-log" id="install-log"></div>
      </div>
      <div class="setup-actions" id="install-actions">
        <button class="btn btn-secondary" id="setup-back">Back</button>
        <button class="btn btn-primary" id="setup-next" style="display:none;">Continue</button>
      </div>
    `;

    const buttonsDiv = body.querySelector("#install-buttons")!;
    const logContainer = body.querySelector("#install-log-container") as HTMLElement;
    const logDiv = body.querySelector("#install-log") as HTMLElement;
    const progressBar = body.querySelector("#install-progress-bar") as HTMLElement;
    const progressLabel = body.querySelector("#install-progress-label") as HTMLElement;
    const nextBtn = body.querySelector("#setup-next") as HTMLElement;

    const checkBrew = async (): Promise<boolean> => {
      const deps = await this.api<DependenciesResponse>("GET", "/setup/dependencies");
      return deps?.dependencies?.find((d) => d.id === "homebrew")?.installed ?? false;
    };

    const hasBrew = await checkBrew();

    if (!hasBrew) {
      const brewBtn = document.createElement("button");
      brewBtn.className = "btn btn-primary btn-lg setup-install-btn";
      brewBtn.id = "btn-install-homebrew";
      brewBtn.innerHTML = `<span class="setup-install-btn-label">Install Homebrew</span><span class="setup-install-btn-desc">Package manager — required for everything else</span>`;
      buttonsDiv.appendChild(brewBtn);

      const depBtn = document.createElement("button");
      depBtn.className = "btn btn-secondary btn-lg setup-install-btn";
      depBtn.id = "btn-install-deps";
      depBtn.innerHTML = `<span class="setup-install-btn-label">Install Dependencies</span><span class="setup-install-btn-desc">MetalSharp Wine, GPTK, Mono, DXVK, Wine, and more</span>`;
      depBtn.style.opacity = "0.5";
      depBtn.style.pointerEvents = "none";
      buttonsDiv.appendChild(depBtn);

      brewBtn.addEventListener("click", async () => {
        brewBtn.textContent = "Opening Terminal...";
        (brewBtn as HTMLButtonElement).disabled = true;
        const result = await getAPI().installHomebrew();
        if (result.ok) {
          brewBtn.innerHTML = '<span class="setup-install-btn-label">Homebrew — Terminal Opened</span><span class="setup-install-btn-desc">Complete the install in Terminal, then click below</span>';
          brewBtn.classList.remove("btn-primary");
          brewBtn.classList.add("btn-secondary");
          depBtn.style.opacity = "1";
          depBtn.style.pointerEvents = "auto";
          this.toast("Complete the Homebrew install in Terminal, then come back", "success");
        } else {
          brewBtn.textContent = "Install Homebrew";
          (brewBtn as HTMLButtonElement).disabled = false;
          this.toast(result.error ?? "Homebrew install failed", "error");
        }
      });

      depBtn.addEventListener("click", () => this.startDepInstall(logContainer, logDiv, progressBar, progressLabel, nextBtn));
    } else {
      const depBtn = document.createElement("button");
      depBtn.className = "btn btn-primary btn-lg setup-install-btn";
      depBtn.id = "btn-install-deps";
      depBtn.innerHTML = `<span class="setup-install-btn-label">Install Dependencies</span><span class="setup-install-btn-desc">MetalSharp Wine, GPTK, Mono, DXVK, Wine, and more</span>`;
      buttonsDiv.appendChild(depBtn);

      depBtn.addEventListener("click", () => this.startDepInstall(logContainer, logDiv, progressBar, progressLabel, nextBtn));
    }

    body.querySelector("#setup-back")?.addEventListener("click", () => this.renderSetupStep(0));

    nextBtn.addEventListener("click", () => this.renderSetupStep(2));
  }

  private async startDepInstall(
    logContainer: HTMLElement,
    logDiv: HTMLElement,
    progressBar: HTMLElement,
    progressLabel: HTMLElement,
    nextBtn: HTMLElement
  ) {
    const depBtn = document.getElementById("btn-install-deps") as HTMLButtonElement;
    if (depBtn) {
      depBtn.disabled = true;
      depBtn.style.opacity = "0.5";
    }

    logContainer.style.display = "block";
    logDiv.innerHTML = "";

    const started = await this.api<{ ok: boolean; error?: string }>("POST", "/setup/install-all");
    if (!started?.ok) {
      this.toast(started?.error ?? "Failed to start installation", "error");
      if (depBtn) { depBtn.disabled = false; depBtn.style.opacity = "1"; }
      return;
    }

    const addLog = (text: string, cls: string) => {
      const line = document.createElement("div");
      line.className = `setup-log-line ${cls}`;
      line.textContent = text;
      logDiv.appendChild(line);
      logDiv.scrollTop = logDiv.scrollHeight;
    };

    addLog("Starting installation...", "info");

    const pollInterval = setInterval(async () => {
      const progress = await this.api<{ step: number; total: number; current: string; status: string; log: string; error: string | null }>("GET", "/setup/install-progress");
      if (!progress) return;

      const pct = progress.total > 0 ? Math.round((progress.step / progress.total) * 100) : 0;
      progressBar.style.width = `${pct}%`;
      progressLabel.textContent = `${pct}%`;

      if (progress.status === "done" || progress.status === "skipped") {
        if (progress.status === "done") {
          addLog(`${progress.log}`, "success");
        } else {
          addLog(`${progress.log}`, "warn");
        }
      } else if (progress.status === "error") {
        if (!logDiv.querySelector(`[data-error-step="${progress.step}"]`)) {
          addLog(`${progress.log}`, "error");
          if (progress.error) {
            addLog(`Error: ${progress.error}`, "error");
          }
          const marker = document.createElement("div");
          marker.dataset.errorStep = String(progress.step);
          marker.style.display = "none";
          logDiv.appendChild(marker);
        }
        clearInterval(pollInterval);
        if (depBtn) { depBtn.disabled = false; depBtn.style.opacity = "1"; depBtn.textContent = "Retry"; }
        return;
      } else if (progress.status === "installing") {
        const existing = logDiv.querySelector(`[data-step="${progress.step}"]`);
        if (!existing) {
          const line = document.createElement("div");
          line.className = "setup-log-line active";
          line.dataset.step = String(progress.step);
          line.textContent = progress.log;
          logDiv.appendChild(line);
          logDiv.scrollTop = logDiv.scrollHeight;
        }
      } else if (progress.status === "complete") {
        addLog("All dependencies installed!", "success");
        clearInterval(pollInterval);
        progressBar.style.width = "100%";
        progressLabel.textContent = "100%";
        nextBtn.style.display = "inline-flex";
        this.toast("All dependencies installed!", "success");
        return;
      } else if (progress.status === "starting") {
        addLog(progress.log, "info");
      }
    }, 500);

    setTimeout(() => clearInterval(pollInterval), 30 * 60 * 1000);
  }

  private async renderSetupSteam(container: HTMLElement) {
    this.renderSetupStepIndicator(container, 2);

    const body = document.createElement("div");
    body.className = "setup-body";
    container.appendChild(body);

    const generatedName = await this.api<{ name: string }>("GET", "/setup/device-name");
    const defaultName = this.setupDeviceName || generatedName?.name || "";

    body.innerHTML = `
      <div class="setup-section-header">
        <h1>Set Up Steam</h1>
        <p>Name your device, then launch Steam and log in. MetalSharp uses Windows Steam via MetalSharp Wine to download and authenticate games.</p>
      </div>
      <div class="setup-form">
        <div class="setup-form-group">
          <label class="setup-label">Device Name</label>
          <input type="text" id="device-name-input" value="${this.esc(defaultName)}" placeholder="e.g. Swift-Falcon" class="setup-input" />
          <div class="setup-hint">Identifies your machine to Steam for persistent login.</div>
        </div>
        <div class="setup-form-group">
          <label class="setup-label">Steam Web API Key (optional)</label>
          <input type="password" id="setup-api-key" placeholder="Enter your Steam Web API key..." class="setup-input" />
          <div class="setup-hint">
            Get a free key at <a href="https://steamcommunity.com/dev/apikey" target="_blank" style="color:var(--orange)">steamcommunity.com/dev/apikey</a>
          </div>
        </div>
      </div>
      <div id="steam-launch-status" style="text-align:center;margin:24px 0;">
        <span class="badge badge-warn" style="font-size:14px;padding:12px 24px;">Steam not running</span>
      </div>
      <div class="setup-actions">
        <button class="btn btn-secondary" id="setup-back">Back</button>
        <button class="btn btn-primary btn-lg" id="btn-launch-steam">Launch Steam</button>
        <button class="btn btn-primary" id="setup-next" style="display:none;">Continue</button>
      </div>
    `;

    const statusDiv = body.querySelector("#steam-launch-status") as HTMLElement;
    const launchBtn = body.querySelector("#btn-launch-steam") as HTMLElement;
    const nextBtn = body.querySelector("#setup-next") as HTMLElement;
    const nameInput = document.getElementById("device-name-input") as HTMLInputElement;

    body.querySelector("#setup-back")?.addEventListener("click", () => this.renderSetupStep(1));

    launchBtn.addEventListener("click", async () => {
      launchBtn.textContent = "Launching Steam...";
      (launchBtn as HTMLButtonElement).disabled = true;

      const steamStatus = await this.api<{ installed: boolean; running: boolean }>("GET", "/steam/status");

      if (steamStatus?.running) {
        statusDiv.innerHTML = '<span class="badge badge-ok" style="font-size:14px;padding:12px 24px;">Steam is running</span>';
        launchBtn.style.display = "none";
        nextBtn.style.display = "inline-flex";
        return;
      }

      if (!steamStatus?.installed) {
        statusDiv.innerHTML = '<div class="spinner"></div> <span style="color:var(--text-dim);font-size:13px;">Downloading Steam installer and launching setup...</span>';
        const installResult = await this.api<{ ok: boolean; path?: string; error?: string }>("POST", "/steam/install");
        if (installResult?.ok) {
          statusDiv.innerHTML = '<div class="setup-hint" style="margin-top:8px">Steam setup wizard opened — complete it, then come back and launch Steam.</div>';
        } else {
          statusDiv.innerHTML = `<div class="setup-hint" style="margin-top:8px;color:var(--error)">${installResult?.error ?? "Failed to install Steam"}</div>`;
        }
        launchBtn.textContent = "Launch Steam";
        (launchBtn as HTMLButtonElement).disabled = false;
        return;
      }

      const result = await this.api<{ ok: boolean; pid?: number; error?: string }>("POST", "/steam/launch");
      if (result?.ok) {
        this.wineSteamRunning = true;
        statusDiv.innerHTML = '<div class="spinner"></div> <span style="color:var(--text-dim);font-size:13px;">Waiting for Steam — log in through the Steam window...</span>';

        const pollSteam = setInterval(async () => {
          const s = await this.api<{ installed: boolean; running: boolean }>("GET", "/steam/status");
          if (s?.running) {
            clearInterval(pollSteam);
            this.wineSteamRunning = true;
            statusDiv.innerHTML = '<span class="badge badge-ok" style="font-size:14px;padding:12px 24px;">Steam is running</span>';
            launchBtn.style.display = "none";
            nextBtn.style.display = "inline-flex";
          }
        }, 3000);

        setTimeout(() => clearInterval(pollSteam), 120000);
      } else {
        this.toast(result?.error ?? "Failed to launch Steam", "error");
        launchBtn.textContent = "Launch Steam";
        (launchBtn as HTMLButtonElement).disabled = false;
      }
    });

    nextBtn.addEventListener("click", async () => {
      const name = nameInput?.value?.trim() || defaultName;
      const keyInput = document.getElementById("setup-api-key") as HTMLInputElement;
      const key = keyInput?.value?.trim();

      this.setupDeviceName = name;
      await this.api("POST", "/setup/save", { step: 2, deviceName: name });

      if (key) {
        await this.api("POST", "/steam/save-api-key", { key });
        this.steamApiKey = key;
      }

      this.renderSetupStep(3);
    });
  }

  private async renderSetupComplete(container: HTMLElement) {
    this.renderSetupStepIndicator(container, 3);

    await this.api("POST", "/setup/save", { step: 3, completed: true });

    const body = document.createElement("div");
    body.className = "setup-body";
    container.appendChild(body);

    body.innerHTML = `
      <div class="setup-body">
        <div class="setup-complete">
          <div class="setup-complete-icon">&#10003;</div>
          <h1>You're All Set!</h1>
          <p>MetalSharp is ready. Download games from your library and play them natively on macOS.</p>
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
      await this.loadConfig();
      await this.loadLibrary();
    });
  }

  // === LIBRARY ===

  private renderLibrary() {
    const el = document.getElementById("view-library")!;
    const lib = this.library;

    if (!lib || lib.games.length === 0) {
      const steamStatusBadge = this.wineSteamRunning
        ? '<span class="badge badge-ok" style="margin-left:8px">Steam Running</span>'
        : this.wineSteamInstalled
        ? '<span class="badge badge-warn" style="margin-left:8px">Steam Offline</span>'
        : '';

      el.innerHTML = `
        <div class="library-header">
          <div>
            <h1>Library</h1>
            <p class="subtitle">No games yet ${steamStatusBadge}</p>
          </div>
          <div class="header-actions">
            <button class="btn btn-secondary" id="btn-steam-launch" title="${this.wineSteamRunning ? 'Stop Wine Steam' : 'Start Wine Steam'}">${this.wineSteamRunning ? 'Stop Steam' : 'Start Steam'}</button>
            <input class="control-input" type="text" id="library-search" placeholder="Search games..." />
            <button class="btn btn-secondary" id="btn-scan">Refresh</button>
          </div>
        </div>
        <div class="empty-state">
          <div class="empty-state-icon">&#x1F3AE;</div>
          <h2>No games found</h2>
          <p>Add your Steam API key in Settings to load your library, or download a game manually.</p>
        </div>
      `;
      el.querySelector("#btn-scan")?.addEventListener("click", () => this.loadLibrary());
      el.querySelector("#btn-steam-launch")?.addEventListener("click", () => this.toggleWineSteam());
      return;
    }

    const installedGames = lib.games.filter(g => g.installed);
    const notInstalled = lib.games.filter(g => !g.installed);

    const steamStatusBadge = this.wineSteamRunning
      ? '<span class="badge badge-ok" style="margin-left:8px">Steam Running</span>'
      : this.wineSteamInstalled
      ? '<span class="badge badge-warn" style="margin-left:8px">Steam Offline</span>'
      : '';

    el.innerHTML = `
      <div class="library-header">
        <div>
          <h1>Library</h1>
          <p class="subtitle">${lib.total} games &middot; ${installedGames.length} installed ${steamStatusBadge}</p>
        </div>
        <div class="header-actions">
          <button class="btn btn-secondary" id="btn-steam-launch" title="${this.wineSteamRunning ? 'Stop Wine Steam' : 'Start Wine Steam'}">${this.wineSteamRunning ? 'Stop Steam' : 'Start Steam'}</button>
          <input class="control-input" type="text" id="library-search" placeholder="Search games..." />
          <select class="control-input control-select" id="library-filter">
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

    const filterEl = document.getElementById("library-filter") as HTMLSelectElement;
    if (filterEl && this.libraryFilter !== "all") {
      filterEl.value = this.libraryFilter;
      this.filterGames();
    }

    el.querySelector("#btn-scan")?.addEventListener("click", () => this.loadLibrary());
    el.querySelector("#library-search")?.addEventListener("input", () => this.filterGames());
    el.querySelector("#library-filter")?.addEventListener("change", () => this.filterGames());
    el.querySelector("#btn-steam-launch")?.addEventListener("click", () => this.toggleWineSteam());
  }

  private filterGames() {
    if (!this.library) return;
    const search = (document.getElementById("library-search") as HTMLInputElement)?.value.toLowerCase() ?? "";
    const filter = (document.getElementById("library-filter") as HTMLSelectElement)?.value ?? "all";
    this.libraryFilter = filter;

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
    const isLaunching = this.launchingAppId === game.appid;
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
      actionHtml = `
        <div class="game-card-actions-stack">
          <button class="btn btn-stop" data-action="stop" data-appid="${game.appid}">Stop</button>
          <button class="btn btn-secondary btn-card" data-action="view-steam" data-appid="${game.appid}">View on Steam</button>
        </div>
      `;
    } else if (isLaunching) {
      actionHtml = `<div class="launching-indicator"><div class="spinner"></div><span class="launching-text">Preparing runtime and launching...</span></div>`;
    } else if (game.installed) {
      actionHtml = `
        <div class="game-card-actions-stack">
          <div class="game-card-actions-row">
            <button class="btn btn-play" data-action="play" data-appid="${game.appid}">Play</button>
            <button class="btn btn-secondary btn-card" data-action="view-steam" data-appid="${game.appid}">View on Steam</button>
          </div>
          <div class="game-card-actions-row subtle">
            <select class="launch-method-select" data-appid="${game.appid}" title="${this.esc(this.launchMethodHelp(game))}">
              ${this.launchMethodOptions(game)}
            </select>
            <button class="btn btn-danger btn-card" data-action="uninstall" data-appid="${game.appid}">Uninstall</button>
          </div>
        </div>
      `;
    } else if (this.wineSteamInstalled) {
      actionHtml = `
        <div class="game-card-actions-stack">
          <button class="btn btn-install" data-action="install" data-appid="${game.appid}">Install</button>
          <div class="game-card-hint">${this.steamApiKey ? "Installs through Windows Steam" : "Add a Steam API key for full library metadata"}</div>
        </div>
      `;
    } else {
      actionHtml = `<span class="badge badge-warn">Setup Steam</span>`;
    }

    const method = this.recommendedLaunchMethod(game);
    const size = game.size_bytes ? this.formatBytes(game.size_bytes) : null;

    card.innerHTML = `
      <div class="game-card-banner">
        ${bannerContent}
      </div>
      <div class="game-card-body">
        <div class="game-card-title">${this.esc(game.name)}</div>
        <div class="game-card-meta">
          ${game.installed ? `<span class="badge badge-ok">Installed</span>` : `<span class="badge badge-warn">Not Installed</span>`}
          <span class="game-card-platform">${this.esc(this.launchMethodLabel(method))}</span>
          ${size ? `<span class="game-card-size">${size}</span>` : ""}
        </div>
        <div class="game-card-actions">
          ${actionHtml}
        </div>
      </div>
    `;

    card.querySelectorAll("[data-action]").forEach((actionEl) => {
      actionEl.addEventListener("click", (e) => {
        const btn = e.currentTarget as HTMLElement;
        const action = btn.dataset.action;
        if (action === "play") this.launchGame(game);
        else if (action === "stop") this.stopGame(game);
        else if (action === "install") this.installGame(game);
        else if (action === "uninstall") this.uninstallGame(game);
        else if (action === "view-steam") this.viewOnSteam(game);
      });
    });

    return card;
  }

  private defaultLaunchMethod(appid: number): string {
    if (appid === 105600) return "xna_fna_arm64";
    if (appid === 504230) return "xna_fna_x86";
    if (appid === 312520) return "gptk_wine";
    if (appid === 535520) return "steam";
    if (appid === 620) return "wine_devel";
    if ([945360, 1139900, 2050650].includes(appid)) return "steam";
    if ([1245620, 814380, 1593500].includes(appid)) return "steam_metalfx";
    return "steam_d3dmetal_perf";
  }

  private recommendedLaunchMethod(game: SteamGame): string {
    return game.launch_method ?? this.defaultLaunchMethod(game.appid);
  }

  private launchMethodLabel(method: string): string {
    const labels: Record<string, string> = {
      xna_fna_arm64: "FNA arm64",
      xna_fna_x86: "FNA x86",
      gptk_wine: "GPTK Wine",
      dxvk_wine: "Wine Devel + DXVK",
      metalsharp_wine: "MetalSharp Wine + DXVK",
      wine_devel: "Wine Devel",
      steam: "MetalSharp Wine Steam",
      steam_metalfx: "MetalSharp Wine + MetalFX",
      steam_d3dmetal_perf: "MetalSharp Wine + D3DMetal",
    };
    return labels[method] ?? "Auto";
  }

  private launchMethodOptions(game: SteamGame): string {
    const recommended = this.recommendedLaunchMethod(game);
    const fallbackMethods = [
      "steam_d3dmetal_perf",
      "steam_metalfx",
      "steam",
      "metalsharp_wine",
      "dxvk_wine",
      "wine_devel",
      "gptk_wine",
      "xna_fna_arm64",
      "xna_fna_x86",
    ].filter((method) => method !== recommended);

    const methods = [
      recommended,
      ...fallbackMethods,
    ];

    return methods.map((method, index) => {
      const prefix = index === 0 ? "Recommended" : "Try";
      return `<option value="${method}">${this.esc(`${prefix}: ${this.launchMethodLabel(method)}`)}</option>`;
    }).join("");
  }

  private launchMethodHelp(game: SteamGame): string {
    const recommended = this.launchMethodLabel(this.recommendedLaunchMethod(game));
    return `Recommended: ${recommended}. Try another method only if this game fails to launch.`;
  }

  private async installGame(game: SteamGame) {
    if (!this.wineSteamInstalled) {
      this.toast("Install Windows Steam first (Settings)", "error");
      return;
    }

    if (!this.wineSteamRunning) {
      this.toast("Starting Steam...", "success");
      await this.api("POST", "/steam/launch");
      await new Promise(r => setTimeout(r, 10000));
      this.wineSteamRunning = true;
    }

    this.toast(`Open Steam and install ${game.name}. It will appear here when ready.`, "success");

    if (this.progressInterval) clearInterval(this.progressInterval);
    this.pollingForInstall = game.appid;
    this.progressInterval = setInterval(async () => {
      const lib = await this.api<{ games: SteamGame[] }>("GET", "/steam/library");
      if (lib?.games) {
        const installed = lib.games.find((g: SteamGame) => g.appid === game.appid && g.installed);
        if (installed) {
          if (this.progressInterval) { clearInterval(this.progressInterval); this.progressInterval = null; }
          this.pollingForInstall = null;
          this.toast(`${game.name} installed!`, "success");
          await this.loadLibrary();
        }
      }
    }, 5000);
  }

  private async launchGame(game: SteamGame) {
    const fnaAppids = [105600, 504230];
    const isFna = fnaAppids.includes(game.appid);

    this.launchingAppId = game.appid;
    this.renderLibrary();

    if (!isFna && this.wineSteamInstalled && !this.wineSteamRunning) {
      this.toast("Starting Steam...", "success");
      await this.api("POST", "/steam/launch");
      await new Promise(r => setTimeout(r, 10000));
      this.wineSteamRunning = true;
    }

    this.toast(`Preparing ${game.name}...`, "success");
    await this.api("POST", "/game/prepare", { appid: game.appid });

    this.toast(`Launching ${game.name}...`, "success");
    const launchResult = await this.api<{ ok: boolean; pid?: number; error?: string; gameType?: string }>("POST", "/game/launch-auto", {
      appid: game.appid,
      launchMethod: (document.querySelector(`.launch-method-select[data-appid="${game.appid}"]`) as HTMLSelectElement)?.value ?? "auto",
    });

    this.launchingAppId = null;

    if (launchResult?.ok && launchResult.pid) {
      this.runningPid = launchResult.pid;
      this.runningAppId = game.appid;
      this.toast(`Launched ${game.name}`, "success");
      this.renderLibrary();
    } else {
      this.toast(launchResult?.error ?? `Failed to launch ${game.name}`, "error");
      this.renderLibrary();
    }
  }

  private async viewOnSteam(game: SteamGame) {
    if (!this.wineSteamRunning) {
      this.toast("Start Steam first to open the library view", "error");
      return;
    }
    const result = await this.api<{ ok: boolean; error?: string }>("POST", "/steam/view-game", { appid: game.appid });
    if (result?.ok) this.toast(`Opened ${game.name} in Steam`, "success");
  }

  private async stopGame(game: SteamGame) {
    await this.api("POST", "/kill", { pid: this.runningPid, appid: game.appid });
    this.runningPid = null;
    this.runningAppId = null;
    this.toast(`Stopped ${game.name}`);
    this.renderLibrary();
  }

  private async uninstallGame(game: SteamGame) {
    if (!confirm(`Uninstall ${game.name}? Game files will be deleted.`)) return;
    this.toast(`Uninstalling ${game.name}...`);
    const result = await this.api<{ ok: boolean }>("POST", "/steam/uninstall-game", { appid: game.appid });
    if (result?.ok) {
      this.toast(`Uninstalled ${game.name}`);
      this.loadLibrary();
    } else {
      this.toast(`Failed to uninstall ${game.name}`, "error");
    }
  }

  private renderSettings() {
    const el = document.getElementById("view-settings")!;
    const steam = this.steam;
    const cfg = this.config;

    const savedKey = this.steamApiKey;

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
        <h2>Steam</h2>
        <div class="settings-row">
          <div>
            <div class="settings-label">Wine Steam (MetalSharp)</div>
            <div class="settings-desc">Windows Steam running in MetalSharp Wine — handles downloads, DRM, and game launches</div>
          </div>
          <div class="settings-value">
            <div style="display:flex;gap:8px;align-items:center;">
              ${this.wineSteamInstalled
                ? `<span class="badge badge-ok">Installed</span>`
                : this.metalsharpWineAvailable
                    ? `<button class="btn btn-primary btn-sm" id="btn-install-steam">Install Steam</button>`
                    : `<span class="badge badge-warn">MetalSharp Wine Required</span>`}
              ${this.wineSteamInstalled ? `<button class="btn btn-secondary btn-sm" id="btn-steam-launch">${this.wineSteamRunning ? 'Stop Steam' : 'Start Steam'}</button>` : ''}
            </div>
          </div>
        </div>
        <div class="settings-row">
          <div>
            <div class="settings-label">macOS Steam</div>
            <div class="settings-desc">Native Steam client on this Mac</div>
          </div>
          <div class="settings-value">
            ${steam?.mac_installed ? `<span class="badge badge-ok">Detected</span>` : `<span class="badge badge-warn">Not Found</span>`}
          </div>
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

    el.querySelector("#btn-change-device")?.addEventListener("click", async () => {
      const result = await this.api<{ name: string }>("GET", "/setup/device-name");
      const newName = result?.name;
      if (newName) {
        this.setupDeviceName = newName;
        await this.api("POST", "/setup/save", { deviceName: newName });
        this.toast(`Device name changed to ${newName}`);
        this.renderSettings();
      }
    });

    el.querySelector("#btn-install-steam")?.addEventListener("click", async () => {
      this.toast("Installing Steam via MetalSharp Wine...");
      await this.api("POST", "/steam/install");
      this.toast("Steam installer launched — wait for it to finish, then start Steam");
      await this.loadLibrary();
      this.renderSettings();
    });

    el.querySelector("#btn-steam-launch")?.addEventListener("click", async () => {
      if (this.wineSteamRunning) {
        await this.api("POST", "/steam/stop");
        this.wineSteamRunning = false;
        this.toast("Steam stopped");
      } else {
        this.toast("Starting Steam...", "success");
        await this.api("POST", "/steam/launch");
        this.wineSteamRunning = true;
        this.toast("Steam started — log in through the Steam window", "success");
      }
      this.renderSettings();
      this.renderLibrary();
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

  private isGameInLocalDir(appid: number): boolean {
    if (!this.library) return false;
    return this.library.games.some(g => g.appid === appid && g.installed);
  }

  private async toggleWineSteam() {
    if (this.wineSteamRunning) {
      await this.api("POST", "/steam/stop");
      this.wineSteamRunning = false;
      this.toast("Steam stopped");
    } else {
      this.toast("Starting Steam...", "success");
      const result = await this.api<{ ok: boolean }>("POST", "/steam/launch");
      if (result?.ok) {
        this.wineSteamRunning = true;
        this.toast("Steam started — log in through the Steam window", "success");
      }
    }
    this.renderLibrary();
  }

  private pollWineSteamInstall(game: SteamGame) {
    const interval = setInterval(async () => {
      await this.loadLibrary();
      const installed = this.library?.games.find(g => g.appid === game.appid)?.installed;
      if (installed) {
        clearInterval(interval);
        this.toast(`${game.name} installed!`, "success");
      }
    }, 5000);
    setTimeout(() => clearInterval(interval), 300000);
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
