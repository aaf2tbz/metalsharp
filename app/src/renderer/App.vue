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
const macSteamInstalled = ref(false);
const macSteamRunning = ref(false);
const library = ref<SteamLibrary | null>(null);
const config = ref<AppConfig | null>(null);
const updateStatus = ref<UpdateStatus | null>(null);
const steamApiKey = ref<string | null>(null);
const setupDeviceName = ref("");

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
provide("macSteamInstalled", macSteamInstalled);
provide("macSteamRunning", macSteamRunning);
provide("backendConnected", backendConnected);
provide("backendVersion", backendVersion);
provide("updateStatus", updateStatus);
provide("steamApiKey", steamApiKey);
provide("setupDeviceName", setupDeviceName);
provide("toast", toast);
provide("loadLibrary", loadLibrary);
provide("api", api);

async function loadLibrary() {
  const lib = await api<SteamLibrary>("GET", "/steam/library");
  if (lib) library.value = lib;

  await api<{ steam: SteamStatus }>("GET", "/scan");

  const steamStatus = await api<{
    installed: boolean;
    running: boolean;
    mac_installed: boolean;
    mac_running: boolean;
    metalsharp_wine_available: boolean;
  }>("GET", "/steam/status");
  if (steamStatus) {
    wineSteamInstalled.value = steamStatus.installed;
    wineSteamRunning.value = steamStatus.running;
    macSteamInstalled.value = steamStatus.mac_installed;
    macSteamRunning.value = steamStatus.mac_running;
  }
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
}

async function getSteamApiKey() {
  const result = await api<{ key: string }>("GET", "/steam/api-key");
  steamApiKey.value = result?.key ?? null;
}

function startHealthPolling() {
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
      <component :is="activeView" :key="currentView" />
    </main>
  </template>
  <Toast />
</template>

<style>
.content {
  flex: 1;
  overflow-y: auto;
  padding: 0;
  min-width: 0;
}
</style>
