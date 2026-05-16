<script setup lang="ts">
import { ref, inject, onMounted, type Ref } from "vue";
import { useToast } from "../composables/useToast";
import { api } from "../composables/useApi";
import GameCard from "../components/GameCard.vue";

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

const library = inject<Ref<SteamLibrary | null>>("library")!;
const wineSteamInstalled = inject<Ref<boolean>>("wineSteamInstalled")!;
const wineSteamRunning = inject<Ref<boolean>>("wineSteamRunning")!;
const backendConnected = inject<Ref<boolean>>("backendConnected")!;
const backendVersion = inject<Ref<string | null>>("backendVersion")!;
const reloadLibrary = inject<() => Promise<void>>("loadLibrary")!;

const toast = useToast();
const search = ref("");
const filter = ref<"all" | "installed" | "not_installed">("all");

const runningAppId = ref<number | null>(null);
const runningPid = ref<number | null>(null);
const launchingAppId = ref<number | null>(null);

const filteredGames = ref<SteamGame[]>([]);

function applyFilter() {
  if (!library.value) {
    filteredGames.value = [];
    return;
  }
  let games = library.value.games;
  if (filter.value === "installed") games = games.filter((g) => g.installed);
  if (filter.value === "not_installed") games = games.filter((g) => !g.installed);
  if (search.value) {
    const q = search.value.toLowerCase();
    games = games.filter((g) => g.name.toLowerCase().includes(q));
  }
  filteredGames.value = games;
}

async function toggleSteam() {
  if (wineSteamRunning.value) {
    await api("POST", "/steam/stop");
    wineSteamRunning.value = false;
    toast.show("Steam stopped");
  } else {
    toast.show("Starting Steam...", "success");
    const result = await api<{ ok: boolean }>("POST", "/steam/launch");
    if (result?.ok) {
      wineSteamRunning.value = true;
      toast.show("Steam started — log in through the Steam window", "success");
    }
  }
  reloadLibrary();
}

async function launchGame(game: SteamGame) {
  launchingAppId.value = game.appid;
  const launchResult = await api<{
    ok: boolean;
    pid?: number;
    error?: string;
    engine?: string;
  }>("POST", "/game/launch-auto", { appid: game.appid, launchMethod: "auto" });

  launchingAppId.value = null;

  if (launchResult?.ok && launchResult.pid) {
    runningPid.value = launchResult.pid;
    runningAppId.value = game.appid;
    toast.show(`Launched ${game.name}`, "success");
  } else {
    toast.show(launchResult?.error ?? `Failed to launch ${game.name}`, "error");
  }
}

async function stopGame(game: SteamGame) {
  await api("POST", "/kill", { pid: runningPid.value, appid: game.appid });
  runningPid.value = null;
  runningAppId.value = null;
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
  if (!confirm(`Uninstall ${game.name}? Game files will be deleted.`)) return;
  toast.show(`Uninstalling ${game.name}...`);
  const result = await api<{ ok: boolean }>("POST", "/steam/uninstall-game", { appid: game.appid });
  if (result?.ok) {
    toast.show(`Uninstalled ${game.name}`);
    reloadLibrary();
  } else {
    toast.show(`Failed to uninstall ${game.name}`, "error");
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
      <h1>Library</h1>
      <div class="library-controls">
        <button class="btn btn-secondary" @click="toggleSteam">
          {{ wineSteamRunning ? "Stop Steam" : "Start Steam" }}
        </button>
        <div class="library-controls-center">
          <input
            v-model="search"
            class="control-input"
            type="text"
            placeholder="Search games..."
          />
          <select v-model="filter" class="control-input">
            <option value="all">All Games</option>
            <option value="installed">Installed</option>
            <option value="not_installed">Not Installed</option>
          </select>
        </div>
        <button class="btn btn-secondary" @click="reloadLibrary()">Refresh</button>
      </div>
      <p class="library-stats">
        {{ library?.total ?? 0 }} games &middot; {{ library?.games.filter(g => g.installed).length ?? 0 }} installed
        <span v-if="wineSteamRunning" class="badge badge-ok">Steam Running</span>
        <span v-else-if="wineSteamInstalled" class="badge badge-warn">Steam Offline</span>
        <span class="badge" :class="backendConnected ? 'badge-ok' : 'badge-error'">
          {{ backendConnected ? `Backend${backendVersion ? ' v' + backendVersion : ''}` : 'Backend Offline' }}
        </span>
      </p>
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
        :key="game.appid"
        :game="game"
        :running="runningAppId === game.appid"
        :launching="launchingAppId === game.appid"
        :steam-installed="wineSteamInstalled"
        @play="launchGame(game)"
        @stop="stopGame(game)"
        @install="installGame(game)"
        @uninstall="uninstallGame(game)"
      />
    </div>
  </div>
</template>

<style scoped>
.library-view {
  padding: 24px 28px;
  height: 100%;
  overflow-y: auto;
}

.library-header {
  margin-bottom: 20px;
}
.library-header h1 {
  font-size: 22px;
  font-weight: 600;
  margin-bottom: 12px;
}

.library-controls {
  display: flex;
  align-items: center;
  gap: 10px;
  flex-wrap: wrap;
}
.library-controls-center {
  display: flex;
  gap: 8px;
  flex: 1;
  min-width: 200px;
}
.library-controls-center input {
  flex: 1;
  min-width: 120px;
}

.library-stats {
  margin-top: 8px;
  font-size: 12px;
  color: var(--text-dim);
  display: flex;
  align-items: center;
  gap: 8px;
  flex-wrap: wrap;
}

.game-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(280px, 1fr));
  gap: 16px;
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
