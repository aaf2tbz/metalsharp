<script setup lang="ts">
import { ref, inject, onMounted, type Ref } from "vue";
import { useToast } from "../composables/useToast";
import { api } from "../composables/useApi";
import GameCard from "../components/GameCard.vue";

interface SteamGame {
  library_id?: string;
  library_source?: "steam" | "metalsharp" | "gptk";
  library_source_label?: string;
  appid: number;
  name: string;
  installed: boolean;
  state: "installed" | "not_installed" | "downloading";
  cover_url: string;
  header_url: string;
  size_bytes?: number | null;
  launch_method?: string;
  available_pipelines?: { id: string; name: string; recommended?: boolean }[];
  has_native_build?: boolean;
  can_uninstall?: boolean;
  bottle_id?: string | null;
  bottle_health?: string | null;
  bottle_runtime_assets?: number;
}

interface SteamLibrary {
  ok: boolean;
  total: number;
  installed_count: number;
  games: SteamGame[];
}

const library = inject<Ref<SteamLibrary | null>>("library")!;
const wineSteamInstalled = inject<Ref<boolean>>("wineSteamInstalled")!;
const wineSteamRunning = inject<Ref<boolean>>("wineSteamRunning")!;
const gptkToolkitInstalled = inject<Ref<boolean>>("gptkToolkitInstalled")!;
const gptkSteamInstalled = inject<Ref<boolean>>("gptkSteamInstalled")!;
const gptkSteamInstalling = inject<Ref<boolean>>("gptkSteamInstalling")!;
const gptkSteamRunning = inject<Ref<boolean>>("gptkSteamRunning")!;
const macSteamInstalled = inject<Ref<boolean>>("macSteamInstalled")!;
const macSteamRunning = inject<Ref<boolean>>("macSteamRunning")!;
const backendConnected = inject<Ref<boolean>>("backendConnected")!;
const backendVersion = inject<Ref<string | null>>("backendVersion")!;
const reloadLibrary = inject<() => Promise<void>>("loadLibrary")!;

const toast = useToast();
const search = ref("");
const filter = ref<"all" | "metalsharp_installed" | "gptk_installed" | "not_installed">("all");

const runningLibraryId = ref<string | null>(null);
const runningPid = ref<number | null>(null);
const launchingLibraryId = ref<string | null>(null);

const filteredGames = ref<SteamGame[]>([]);

function isMacSteamLaunch(launchMethod: string) {
  return launchMethod === "mac_steam" || launchMethod === "macos_steam" || launchMethod === "native_steam";
}

function isWineSteamRouteId(launchMethod: string) {
  const method = launchMethod.toLowerCase();
  return [
    "steam",
    "wine_steam",
    "m9",
    "m10",
    "m11",
    "m12",
    "m13",
    "gptk",
    "d3dmetal",
    "m32",
    "dx9",
    "dx10",
    "dx11",
    "dx12",
    "d3d9",
    "d3d10",
    "d3d11",
    "d3d12",
    "d3d9_metal",
    "dxmt_metal",
    "dxmt_metal12",
    "steam_d3dmetal_gptk",
    "wined3d_32",
  ].includes(method);
}

function recommendedLaunchMethod(game: SteamGame) {
  return game.launch_method ?? game.available_pipelines?.find((pipeline) => pipeline.recommended)?.id;
}

function isWineSteamRouteLaunch(game: SteamGame, launchMethod: string) {
  const method = launchMethod.toLowerCase();
  if (method === "auto") {
    const recommended = recommendedLaunchMethod(game);
    return recommended ? isWineSteamRouteId(recommended) : false;
  }
  return isWineSteamRouteId(method);
}

function applyFilter() {
  if (!library.value) {
    filteredGames.value = [];
    return;
  }
  let games = library.value.games;
  if (filter.value === "metalsharp_installed") games = games.filter((g) => g.installed && g.library_source === "metalsharp");
  if (filter.value === "gptk_installed") games = games.filter((g) => g.installed && g.library_source === "gptk");
  if (filter.value === "not_installed") games = games.filter((g) => !g.installed);
  if (search.value) {
    const q = search.value.toLowerCase();
    games = games.filter((g) => g.name.toLowerCase().includes(q));
  }
  filteredGames.value = games;
}

async function toggleSteam() {
  if (wineSteamRunning.value) {
    const result = await api<{ ok: boolean; running?: boolean; error?: string }>("POST", "/steam/stop");
    if (result?.ok && result.running === false) {
      wineSteamRunning.value = false;
      toast.show("Wine Steam stopped");
    } else {
      wineSteamRunning.value = result?.running ?? true;
      toast.show(result?.error ?? "Wine Steam is still running", "error");
    }
  } else {
    if (gptkSteamRunning.value) {
      if (!confirm("Stop GPTK Steam and start Wine Steam?")) return;
      const stopResult = await api<{ ok: boolean; running?: boolean; error?: string }>("POST", "/steam/gptk-stop");
      if (!stopResult?.ok || stopResult.running !== false) {
        gptkSteamRunning.value = stopResult?.running ?? true;
        toast.show(stopResult?.error ?? "GPTK Steam is still running", "error");
        return;
      }
      gptkSteamRunning.value = false;
    }
    toast.show("Starting Steam...", "success");
    const result = await api<{ ok: boolean; running?: boolean; error?: string }>("POST", "/steam/launch");
    if (result?.ok) {
      wineSteamRunning.value = result.running ?? true;
      toast.show("Steam started — log in through the Steam window", "success");
    } else {
      toast.show(result?.error ?? "Could not start Wine Steam", "error");
    }
  }
  reloadLibrary();
}

async function toggleMacSteam() {
  if (!macSteamInstalled.value) {
    const result = await api<{ ok: boolean; installed?: boolean; error?: string }>("POST", "/steam/mac-install");
    if (result?.ok) toast.show(result.installed ? "macOS Steam is already installed" : "Steam download page opened", "success");
    else toast.show(result?.error ?? "Could not open macOS Steam installer", "error");
    return;
  }
  if (macSteamRunning.value) {
    const result = await api<{ ok: boolean; running?: boolean; error?: string }>("POST", "/steam/mac-stop");
    if (result?.ok && result.running === false) {
      macSteamRunning.value = false;
      toast.show("Mac Steam stopped");
    } else {
      macSteamRunning.value = result?.running ?? true;
      toast.show(result?.error ?? "Mac Steam is still running", "error");
    }
  } else {
    if (wineSteamRunning.value) {
      if (!confirm("Stop Wine Steam and start Mac Steam?")) return;
      const stopResult = await api<{ ok: boolean; running?: boolean; error?: string }>("POST", "/steam/stop");
      if (!stopResult?.ok || stopResult.running !== false) {
        wineSteamRunning.value = stopResult?.running ?? true;
        toast.show(stopResult?.error ?? "Wine Steam is still running", "error");
        return;
      }
      wineSteamRunning.value = false;
    }
    toast.show("Starting Mac Steam...", "success");
    const result = await api<{ ok: boolean }>("POST", "/steam/mac-launch");
    if (result?.ok) {
      macSteamRunning.value = true;
      toast.show("Mac Steam started", "success");
    }
  }
  reloadLibrary();
}

async function toggleGptkSteam() {
  if (!gptkToolkitInstalled.value) {
    const result = await api<{ ok: boolean; installed?: boolean; download_required?: boolean; error?: string }>(
      "POST",
      "/steam/gptk-toolkit-install",
    );
    if (result?.ok && result.installed) {
      gptkToolkitInstalled.value = true;
      toast.show("Game Porting Toolkit runtime installed", "success");
    } else {
      toast.show(
        result?.ok && result.download_required
          ? "Game Porting Toolkit download page opened"
          : (result?.error ?? "Could not set up GPTK runtime"),
        result?.ok ? "success" : "error",
      );
      return;
    }
  }
  if (gptkSteamInstalling.value) {
    toast.show("GPTK Steam setup is already running", "success");
    return;
  }
  if (!gptkSteamInstalled.value) {
    toast.show("Starting GPTK Steam setup...", "success");
    const result = await api<{ ok: boolean; installing?: boolean; installed?: boolean; error?: string }>("POST", "/steam/gptk-install");
    if (result?.ok) {
      gptkSteamInstalling.value = result.installing ?? false;
      gptkSteamInstalled.value = result.installed ?? gptkSteamInstalled.value;
      toast.show(result.installed ? "GPTK Steam is already installed" : "Steam installer launched in GPTK Wine", "success");
      if (result.installing) pollGptkSteamInstall();
    } else {
      toast.show(result?.error ?? "Could not start GPTK Steam setup", "error");
    }
    reloadLibrary();
    return;
  }
  if (gptkSteamRunning.value) {
    const result = await api<{ ok: boolean; running?: boolean; error?: string }>("POST", "/steam/gptk-stop");
    if (result?.ok && result.running === false) {
      gptkSteamRunning.value = false;
      toast.show("GPTK Steam stopped");
    } else {
      gptkSteamRunning.value = result?.running ?? true;
      toast.show(result?.error ?? "GPTK Steam is still running", "error");
    }
  } else {
    if (wineSteamRunning.value) {
      if (!confirm("Stop Wine Steam and start GPTK Steam?")) return;
      const stopResult = await api<{ ok: boolean; running?: boolean; error?: string }>("POST", "/steam/stop");
      if (!stopResult?.ok || stopResult.running !== false) {
        wineSteamRunning.value = stopResult?.running ?? true;
        toast.show(stopResult?.error ?? "Wine Steam is still running", "error");
        return;
      }
      wineSteamRunning.value = false;
    }
    toast.show("Starting GPTK Steam...", "success");
    const result = await api<{ ok: boolean; pid?: number; running?: boolean; installed?: boolean; error?: string }>("POST", "/steam/gptk-launch");
    if (result?.ok) {
      gptkSteamInstalled.value = result.installed ?? gptkSteamInstalled.value;
      gptkSteamRunning.value = result.running ?? true;
      toast.show("GPTK Steam started", "success");
    } else {
      toast.show(result?.error ?? "Could not start GPTK Steam", "error");
    }
  }
  reloadLibrary();
}

function pollGptkSteamInstall() {
  let attempts = 0;
  const poll = setInterval(async () => {
    attempts += 1;
    const status = await api<{
      gptk_steam_installed?: boolean;
      gptk_installing?: boolean;
      gptk_running?: boolean;
      gptk_install_progress?: { phase: string; message: string; error?: string | null };
    }>("GET", "/steam/status");
    if (!status) return;
    gptkSteamInstalling.value = status.gptk_installing ?? false;
    gptkSteamInstalled.value = status.gptk_steam_installed ?? false;
    gptkSteamRunning.value = status.gptk_running ?? gptkSteamRunning.value;
    if (status.gptk_steam_installed) {
      clearInterval(poll);
      gptkSteamInstalling.value = false;
      toast.show("GPTK Steam is ready", "success");
      reloadLibrary();
    } else if (status.gptk_install_progress?.phase === "error") {
      clearInterval(poll);
      gptkSteamInstalling.value = false;
      toast.show(status.gptk_install_progress.error ?? status.gptk_install_progress.message, "error");
    } else if (attempts > 180) {
      clearInterval(poll);
      toast.show("GPTK Steam setup is still running. Check the Steam installer window.", "error");
    }
  }, 2000);
}

async function launchGame(game: SteamGame, launchMethod = "auto") {
  const libraryId = game.library_id ?? String(game.appid);
  if (isMacSteamLaunch(launchMethod) && wineSteamRunning.value) {
    if (!confirm(`Stop Wine Steam and launch ${game.name} through MacOS Steam?`)) return;
    const stopResult = await api<{ ok: boolean; running?: boolean; error?: string }>("POST", "/steam/stop");
    if (!stopResult?.ok || stopResult.running !== false) {
      wineSteamRunning.value = stopResult?.running ?? true;
      toast.show(stopResult?.error ?? "Wine Steam is still running", "error");
      return;
    }
    wineSteamRunning.value = false;
  }

  launchingLibraryId.value = libraryId;
  const useWineSteamRoute = isWineSteamRouteLaunch(game, launchMethod);
  const launchEndpoint = useWineSteamRoute ? "/steam/launch-game" : "/game/launch-auto";
  const launchResult = await api<{
    ok: boolean;
    pid?: number;
    error?: string;
    engine?: string;
    gameType?: string;
    steam_runtime?: string;
  }>("POST", launchEndpoint, {
    appid: game.appid,
    launchMethod,
  });

  launchingLibraryId.value = null;

  if (launchResult?.ok && launchResult.pid) {
    runningPid.value = launchResult.pid;
    runningLibraryId.value = libraryId;
    if (
      launchResult.steam_runtime === "offline_gptk" ||
      launchMethod.toLowerCase() === "m13" ||
      launchMethod.toLowerCase() === "gptk" ||
      launchMethod.toLowerCase() === "d3dmetal"
    ) {
      gptkSteamRunning.value = true;
      wineSteamRunning.value = false;
    } else if (useWineSteamRoute) {
      wineSteamRunning.value = true;
    }
    if (isMacSteamLaunch(launchMethod) || launchResult.gameType === "macos_steam") macSteamRunning.value = true;
    toast.show(`Launched ${game.name}`, "success");
  } else {
    toast.show(launchResult?.error ?? `Failed to launch ${game.name}`, "error");
  }
}

async function stopGame(game: SteamGame) {
  await api("POST", "/kill", { pid: runningPid.value, appid: game.appid });
  runningPid.value = null;
  runningLibraryId.value = null;
  toast.show(`Stopped ${game.name}`);
}

async function installGame(game: SteamGame) {
  if (!wineSteamInstalled.value) {
    toast.show("Install Windows Steam first (Settings)", "error");
    return;
  }
  if (!wineSteamRunning.value) {
    toast.show("Starting Steam...", "success");
    await api("POST", "/steam/launch");
    wineSteamRunning.value = true;
  }
  toast.show(`Open Steam and install ${game.name}. It will appear here when ready.`, "success");
}

async function uninstallGame(game: SteamGame) {
  if (game.can_uninstall === false) {
    toast.show(`${game.name} is only installed in macOS Steam. Uninstall it from macOS Steam.`, "error");
    return;
  }
  if (!confirm(`Uninstall ${game.name}? Game files will be deleted.`)) return;
  toast.show(`Uninstalling ${game.name}...`);
  const result = await api<{ ok: boolean; error?: string }>("POST", "/steam/uninstall-game", { appid: game.appid });
  if (result?.ok) {
    toast.show(`Uninstalled ${game.name}`);
    reloadLibrary();
  } else {
    toast.show(result?.error ?? `Failed to uninstall ${game.name}`, "error");
  }
}

onMounted(() => {
  applyFilter();
});

import { watch } from "vue";
watch([library, search, filter], applyFilter);
</script>

<template>
  <div class="library-view">
    <div class="library-header">
      <div class="library-title-row">
        <div>
          <h1>Library</h1>
          <p class="library-counts">
            {{ library?.total ?? 0 }} games &middot; {{ library?.games.filter(g => g.installed).length ?? 0 }} installed
          </p>
        </div>
        <div class="library-status-strip">
          <span v-if="wineSteamRunning" class="badge badge-ok">Steam Running</span>
          <span v-else-if="wineSteamInstalled" class="badge badge-warn">Steam Offline</span>
          <span v-if="gptkSteamRunning" class="badge badge-ok">GPTK Steam Running</span>
          <span v-else-if="gptkSteamInstalling" class="badge badge-warn">GPTK Steam Setup</span>
          <span v-else-if="gptkSteamInstalled" class="badge badge-warn">GPTK Steam Offline</span>
          <span v-else-if="gptkToolkitInstalled" class="badge badge-warn">GPTK Steam Missing</span>
          <span v-if="macSteamRunning" class="badge badge-ok">Mac Steam Running</span>
          <span v-else-if="macSteamInstalled" class="badge badge-warn">Mac Steam Offline</span>
          <span class="badge" :class="backendConnected ? 'badge-ok' : 'badge-error'">
            {{ backendConnected ? `Backend${backendVersion ? ' v' + backendVersion : ''}` : 'Backend Offline' }}
          </span>
        </div>
      </div>
      <div class="library-controls">
        <div class="library-launch-actions">
          <button class="btn btn-secondary library-control-button" title="Wine Steam" @click="toggleSteam">
          <svg class="control-icon" width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
            <circle cx="12" cy="12" r="3" /><path d="M3 12h6" /><path d="M15 12h6" /><path d="M12 3v6" /><path d="M12 15v6" />
          </svg>
          <span class="control-label">{{ wineSteamRunning ? "Stop Wine Steam" : "Start Wine Steam" }}</span>
          </button>
          <button
            class="btn btn-secondary library-control-button"
            title="GPTK Steam"
            @click="toggleGptkSteam"
          >
          <svg class="control-icon" width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
            <path d="M4 15a8 8 0 0 1 8-8h7" />
            <path d="m15 3 4 4-4 4" />
            <path d="M20 15a8 8 0 0 1-8 8H5" />
            <path d="m9 19-4 4 4 4" transform="translate(0 -4)" />
          </svg>
          <span class="control-label">
            {{ !gptkToolkitInstalled ? "Setup GPTK Wine" : gptkSteamInstalling ? "Installing GPTK Steam" : !gptkSteamInstalled ? "Install GPTK Steam" : gptkSteamRunning ? "Stop GPTK Steam" : "Start GPTK Steam" }}
          </span>
          </button>
          <button
            class="btn btn-secondary library-control-button"
            title="MacOS Steam"
            @click="toggleMacSteam"
          >
          <svg class="control-icon" width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
            <rect x="5" y="4" width="14" height="17" rx="2" /><path d="M9 4V2h6v2" /><path d="M9 18h6" />
          </svg>
          <span class="control-label">
            {{ !macSteamInstalled ? "Install macOS Steam" : macSteamRunning ? "Stop MacOS Steam" : "Start MacOS Steam" }}
          </span>
          </button>
        </div>
        <div class="library-controls-center">
          <input
            v-model="search"
            class="control-input"
            type="text"
            placeholder="Search games..."
          />
          <div class="library-tabs" role="tablist" aria-label="Library filters">
            <button class="library-tab" :class="{ active: filter === 'all' }" type="button" @click="filter = 'all'">All</button>
            <button
              class="library-tab"
              :class="{ active: filter === 'metalsharp_installed' }"
              type="button"
              @click="filter = 'metalsharp_installed'"
            >
              MetalSharp Installed
            </button>
            <button
              class="library-tab"
              :class="{ active: filter === 'gptk_installed' }"
              type="button"
              @click="filter = 'gptk_installed'"
            >
              GPTK Installed
            </button>
            <button
              class="library-tab"
              :class="{ active: filter === 'not_installed' }"
              type="button"
              @click="filter = 'not_installed'"
            >
              Not Installed
            </button>
          </div>
        </div>
        <button class="btn btn-secondary library-control-button refresh-button" title="Refresh" @click="reloadLibrary()">
          <svg class="control-icon" width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
            <path d="M21 12a9 9 0 0 1-15.5 6.2" /><path d="M3 12A9 9 0 0 1 18.5 5.8" /><path d="M18 2v4h4" /><path d="M6 22v-4H2" />
          </svg>
          <span class="control-label">Refresh</span>
        </button>
      </div>
    </div>

    <div v-if="!library || library.games.length === 0" class="empty-state">
      <div class="empty-icon">
        <svg width="48" height="48" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5"><rect x="2" y="6" width="20" height="12" rx="2"/><line x1="6" y1="10" x2="6" y2="14"/><line x1="10" y1="10" x2="10" y2="14"/></svg>
      </div>
      <h2>No games found</h2>
      <p>Add your Steam API key in Settings to load your library, or download a game manually.</p>
    </div>

    <div v-else class="game-grid">
      <GameCard
        v-for="game in filteredGames"
        :key="game.library_id ?? game.appid"
        :game="game"
        :running="runningLibraryId === (game.library_id ?? String(game.appid))"
        :launching="launchingLibraryId === (game.library_id ?? String(game.appid))"
        :steam-installed="wineSteamInstalled"
        @play="launchGame(game, $event)"
        @stop="stopGame(game)"
        @install="installGame(game)"
        @uninstall="uninstallGame(game)"
      />
    </div>
  </div>
</template>

<style scoped>
.library-view {
  padding: 24px 28px 32px;
  height: 100%;
  overflow-y: auto;
  background: var(--bg-deep);
}

.library-header {
  margin: -24px -28px 24px;
  padding: 28px 28px 20px;
  background: var(--page-header-bg);
  border-bottom: 1px solid var(--border);
}
.library-title-row {
  display: flex;
  align-items: flex-start;
  justify-content: space-between;
  gap: 18px;
  margin-bottom: 18px;
}
.library-header h1 {
  font-size: 24px;
  font-weight: 750;
  line-height: 1.1;
}
.library-counts {
  margin-top: 6px;
  color: var(--text-dim);
  font-size: 12px;
}
.library-status-strip {
  display: flex;
  align-items: center;
  justify-content: flex-end;
  gap: 8px;
  flex-wrap: wrap;
  min-width: 220px;
}

.library-controls {
  display: grid;
  grid-template-columns: minmax(260px, auto) minmax(280px, 1fr) auto;
  align-items: center;
  gap: 12px;
  min-width: 0;
}
.library-launch-actions {
  display: flex;
  align-items: center;
  gap: 10px;
  min-width: 0;
}
.library-controls-center {
  display: grid;
  grid-template-columns: minmax(150px, 1fr) minmax(360px, auto);
  gap: 10px;
  min-width: 0;
}
.library-controls-center input {
  min-width: 0;
}
.library-controls-center select {
  min-width: 0;
}
.library-tabs {
  display: grid;
  grid-template-columns: repeat(4, minmax(0, auto));
  align-items: center;
  gap: 4px;
  min-width: 0;
  padding: 3px;
  border: 1px solid var(--border);
  border-radius: var(--radius-sm);
  background: var(--bg-surface);
}
.library-tab {
  min-height: 30px;
  min-width: 0;
  padding: 0 9px;
  border: 0;
  border-radius: var(--radius-sm);
  background: transparent;
  color: var(--text-dim);
  font-size: 12px;
  font-weight: 700;
  white-space: nowrap;
  cursor: pointer;
}
.library-tab.active {
  background: var(--accent);
  color: var(--bg-deep);
}
.library-tab:hover:not(.active) {
  background: color-mix(in srgb, var(--accent) 10%, transparent);
  color: var(--text-primary);
}
.library-control-button {
  flex: 0 1 auto;
  min-width: 0;
}
.control-icon {
  flex: 0 0 15px;
}
.control-label {
  overflow: hidden;
  text-overflow: ellipsis;
}

@media (max-width: 1040px) {
  .library-controls {
    grid-template-columns: 1fr;
  }
  .refresh-button {
    justify-self: start;
  }
}

@media (max-width: 880px) {
  .library-title-row {
    flex-direction: column;
    gap: 10px;
  }
  .library-status-strip {
    justify-content: flex-start;
    min-width: 0;
  }
  .library-controls {
    gap: 6px;
  }
  .library-control-button {
    min-width: 34px;
  }
  .library-launch-actions {
    display: grid;
    grid-template-columns: repeat(3, minmax(0, 1fr));
  }
  .library-controls-center {
    grid-template-columns: 1fr;
  }
  .library-tabs {
    grid-template-columns: repeat(2, minmax(0, 1fr));
  }
}

@media (max-width: 620px) {
  .library-view {
    padding-left: 16px;
    padding-right: 16px;
  }
  .library-header {
    margin-left: -16px;
    margin-right: -16px;
    padding-left: 16px;
    padding-right: 16px;
  }
  .library-launch-actions {
    grid-template-columns: 1fr;
  }
}

.game-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(300px, 1fr));
  gap: 18px;
}

.empty-state {
  text-align: center;
  padding: 80px 20px;
  color: var(--text-dim);
}
.empty-icon {
  margin-bottom: 16px;
  opacity: 0.4;
}
.empty-state h2 {
  font-size: 16px;
  margin-bottom: 8px;
  color: var(--text-secondary);
}
.empty-state p {
  font-size: 13px;
  max-width: 360px;
  margin: 0 auto;
  line-height: 1.5;
}
</style>
