<script setup lang="ts">
import { ref, computed, onMounted, provide, type Component } from "vue";
import Sidebar from "./components/Sidebar.vue";
import Toast from "./components/Toast.vue";
import SetupWizard from "./components/SetupWizard.vue";
import MigrationView from "./components/MigrationView.vue";
import LibraryView from "./views/LibraryView.vue";
import SharpView from "./views/SharpView.vue";
import LogsView from "./views/LogsView.vue";
import SettingsView from "./views/SettingsView.vue";
import { useTheme } from "./composables/useTheme";
import { useToast } from "./composables/useToast";
import { getAPI, api } from "./composables/useApi";
import type { AppConfig, UpdateStatus, SteamStatus } from "./api-types";

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

const currentView = ref("library");
const showSetup = ref(false);
const showMigration = ref(false);
const backendConnected = ref(false);
const backendVersion = ref<string | null>(null);
const wineSteamInstalled = ref(false);
const wineSteamRunning = ref(false);
const gptkToolkitInstalled = ref(false);
const gptkSteamInstalled = ref(false);
const gptkSteamInstalling = ref(false);
const gptkSteamRunning = ref(false);
const macSteamInstalled = ref(false);
const macSteamRunning = ref(false);
const library = ref<SteamLibrary | null>(null);
const config = ref<AppConfig | null>(null);
const updateStatus = ref<UpdateStatus | null>(null);
const steamApiKey = ref<string | null>(null);
const setupDeviceName = ref("");

const updateDownloading = ref(false);
const updateProgress = ref(0);
const updateMessage = ref("");

const { theme, toggle: toggleTheme } = useTheme();
const toast = useToast();

const viewMap: Record<string, Component> = {
  library: LibraryView,
  "sharp-library": SharpView,
  logs: LogsView,
  settings: SettingsView,
};

const activeView = computed(() => viewMap[currentView.value] ?? LibraryView);

provide("library", library);
provide("config", config);
provide("wineSteamInstalled", wineSteamInstalled);
provide("wineSteamRunning", wineSteamRunning);
provide("gptkToolkitInstalled", gptkToolkitInstalled);
provide("gptkSteamInstalled", gptkSteamInstalled);
provide("gptkSteamInstalling", gptkSteamInstalling);
provide("gptkSteamRunning", gptkSteamRunning);
provide("macSteamInstalled", macSteamInstalled);
provide("macSteamRunning", macSteamRunning);
provide("backendConnected", backendConnected);
provide("backendVersion", backendVersion);
provide("updateStatus", updateStatus);
provide("updateDownloading", updateDownloading);
provide("updateProgress", updateProgress);
provide("updateMessage", updateMessage);
provide("startUpdateDownload", startUpdateDownload);
provide("steamApiKey", steamApiKey);
provide("setupDeviceName", setupDeviceName);
provide("toast", toast);
provide("loadLibrary", loadLibrary);
provide("api", api);

async function refreshSteamStatus() {
  const steamStatus = await api<{
    installed: boolean;
    running: boolean;
    gptk_installed: boolean;
    gptk_toolkit_installed?: boolean;
    gptk_steam_installed?: boolean;
    gptk_installing?: boolean;
    gptk_running: boolean;
    gptk_synced: boolean;
    mac_installed: boolean;
    mac_running: boolean;
    metalsharp_wine_available: boolean;
  }>("GET", "/steam/status");
  if (steamStatus) {
    wineSteamInstalled.value = steamStatus.installed;
    wineSteamRunning.value = steamStatus.running;
    gptkToolkitInstalled.value = steamStatus.gptk_toolkit_installed ?? steamStatus.gptk_installed;
    gptkSteamInstalled.value = steamStatus.gptk_steam_installed ?? steamStatus.gptk_synced;
    gptkSteamInstalling.value = steamStatus.gptk_installing ?? false;
    gptkSteamRunning.value = steamStatus.gptk_running;
    macSteamInstalled.value = steamStatus.mac_installed;
    macSteamRunning.value = steamStatus.mac_running;
  }
}

async function loadLibrary() {
  const lib = await api<SteamLibrary>("GET", "/steam/library");
  if (lib) library.value = lib;

  await api<{ steam: SteamStatus }>("GET", "/scan");
  await refreshSteamStatus();
}

async function checkBackend() {
  try {
    const res = await getAPI().request("GET", "/status");
    backendConnected.value = res.ok;
    if (res.ok) {
      const status = res.data as { version?: string } | undefined;
      if (status?.version) backendVersion.value = status.version;
    }
  } catch {
    backendConnected.value = false;
  }
}

async function checkForUpdates() {
  const result = await api<UpdateStatus>("GET", "/update/check");
  if (result) updateStatus.value = result;
  if (result?.ok && result.available) {
    toast.show(`Update available: v${result.latest_version}`, "success");
  }
}

async function startUpdateDownload() {
  if (updateDownloading.value) return;
  const backend = getAPI();
  const pid = await backend.backendGetPid();
  if (!pid) {
    toast.show("Cannot get backend PID", "error");
    return;
  }
  const ready = await backend.updaterEnsureReady();
  if (!ready) {
    toast.show("Updater not available", "error");
    return;
  }
  updateDownloading.value = true;
  updateProgress.value = 0;
  updateMessage.value = "Starting download...";

  const startResult = await api<{ ok: boolean; error?: string }>("POST", "/update/start");
  if (!startResult?.ok) {
    toast.show(startResult?.error ?? "Failed to start download", "error");
    updateDownloading.value = false;
    return;
  }

  const pollDownload = setInterval(async () => {
    const progress = await api<{ status: string; percent: number; message: string; error: string | null }>("GET", "/update/progress");
    if (!progress) return;
    updateProgress.value = progress.percent ?? 0;
    updateMessage.value = progress.message ?? "";
    if (progress.status === "downloaded" || progress.status === "complete") {
      clearInterval(pollDownload);
      const dmgResult = await api<{ ok: boolean; path?: string }>("GET", "/update/dmg-path");
      if (!dmgResult?.path) {
        toast.show("Download complete but DMG not found", "error");
        updateDownloading.value = false;
        return;
      }
      const targetVersion = updateStatus.value?.latest_version ?? "";
      const spawnResult = await backend.updaterSpawnInstall(dmgResult.path, pid, targetVersion);
      if (!spawnResult?.ok) {
        toast.show(spawnResult?.error ?? "Failed to start installer", "error");
        updateDownloading.value = false;
        return;
      }
      updateMessage.value = "Installing update...";
      updateProgress.value = 90;
      const pollInstall = setInterval(async () => {
        const installStatus = await backend.updaterInstallStatus();
        if (!installStatus) return;
        updateProgress.value = installStatus.percent ?? 90;
        updateMessage.value = installStatus.message ?? "Installing...";
        if (installStatus.phase === "complete") {
          clearInterval(pollInstall);
          updateDownloading.value = false;
          updateProgress.value = 100;
          updateMessage.value = "";
          toast.show("Update installed — restarting...", "success");
          await new Promise((r) => setTimeout(r, 2000));
          backend.quitApp();
        }
      }, 1000);
    } else if (progress.status === "error") {
      clearInterval(pollDownload);
      updateDownloading.value = false;
      toast.show(progress.error ?? "Download failed", "error");
    }
  }, 500);
}

async function getSteamApiKey() {
  const result = await api<{ key: string }>("GET", "/steam/api-key");
  steamApiKey.value = result?.key ?? null;
}

function startHealthPolling() {
  setInterval(refreshSteamStatus, 5000);

  setInterval(async () => {
    const prev = backendConnected.value;
    backendConnected.value = await getAPI().isBackendAlive();
    if (backendConnected.value) {
      try {
        const status = await api<{ ok?: boolean; version?: string }>("GET", "/status");
        if (status?.version) backendVersion.value = status.version;
      } catch {}
    } else {
      backendVersion.value = null;
    }
    if (prev && !backendConnected.value) toast.show("Backend connection lost", "error");
    else if (!prev && backendConnected.value) toast.show("Backend connected", "success");
  }, 120000);
}

function onSetupDone() {
  showSetup.value = false;
  initApp();
}

async function initApp() {
  await loadLibrary();
  await getSteamApiKey();
  const state = await api<{ deviceName?: string }>("GET", "/setup/state");
  if (state?.deviceName) setupDeviceName.value = state.deviceName;
  startHealthPolling();
}

onMounted(async () => {
  await checkBackend();
  const migrationMode = await getAPI().isMigrationMode?.();
  if (migrationMode) {
    showMigration.value = true;
    return;
  }
  const firstLaunch = await getAPI().isFirstLaunch();
  if (firstLaunch) {
    showSetup.value = true;
    return;
  }
  await initApp();
  checkForUpdates();
});
</script>

<template>
  <MigrationView v-if="showMigration" />
  <SetupWizard v-else-if="showSetup" @done="onSetupDone()" />
  <template v-else>
    <Sidebar
      :current-view="currentView"
      :theme="theme"
      @navigate="currentView = $event"
      @toggle-theme="toggleTheme()"
    />
    <main class="content">
      <div class="drag-strip"></div>
      <div v-if="updateStatus?.ok && updateStatus?.available" class="update-banner">
        <span class="update-banner-text" v-if="!updateDownloading">MetalSharp v{{ updateStatus.latest_version }} is available</span>
        <span class="update-banner-text" v-else>{{ updateMessage }}</span>
        <div v-if="updateDownloading" class="update-banner-progress">
          <div class="update-banner-progress-fill" :style="{ width: updateProgress + '%' }"></div>
        </div>
        <button v-if="!updateDownloading" class="update-banner-btn" @click="startUpdateDownload">Download &amp; Install</button>
      </div>
      <component :is="activeView" :key="currentView" />
    </main>
  </template>
  <Toast />
</template>

<style>
.drag-strip {
  height: 38px;
  background: var(--page-header-bg);
  -webkit-app-region: drag;
  flex-shrink: 0;
}
.update-banner {
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 12px;
  padding: 8px 16px;
  background: var(--accent);
  color: #1b2838;
  font-size: 12px;
  font-weight: 600;
  cursor: pointer;
  flex-shrink: 0;
}
.update-banner:hover {
  opacity: 0.9;
}
.update-banner-text {
  white-space: nowrap;
}
.update-banner-btn {
  background: rgba(0, 0, 0, 0.15);
  border: none;
  color: #1b2838;
  padding: 3px 14px;
  border-radius: 4px;
  font-size: 11px;
  font-weight: 700;
  cursor: pointer;
}
.update-banner-btn:hover {
  background: rgba(0, 0, 0, 0.25);
}
.update-banner-progress {
  width: 120px;
  height: 4px;
  background: rgba(0, 0, 0, 0.15);
  border-radius: 2px;
  overflow: hidden;
}
.update-banner-progress-fill {
  height: 100%;
  background: #1b2838;
  border-radius: 2px;
  transition: width 0.3s ease;
}
.content {
  flex: 1;
  overflow-y: auto;
  padding: 0;
  min-width: 0;
  display: flex;
  flex-direction: column;
  background: var(--bg-deep);
}
</style>
