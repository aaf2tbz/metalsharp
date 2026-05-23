<script setup lang="ts">
import { ref, inject, onMounted, type Ref } from "vue";
import { useToast } from "../composables/useToast";
import { api, getAPI } from "../composables/useApi";
import type { AppConfig, UpdateStatus } from "../api-types";

interface CacheSummary {
  bytes: number;
  files: number;
  directories: number;
  apps: number;
  status: "missing" | "empty" | "active";
  path: string;
  last_modified: string | null;
}

const config = inject<Ref<AppConfig | null>>("config")!;
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
const updateStatus = inject<Ref<UpdateStatus | null>>("updateStatus")!;
const updateDownloading = inject<Ref<boolean>>("updateDownloading")!;
const updateProgress = inject<Ref<number>>("updateProgress")!;
const updateMessage = inject<Ref<string>>("updateMessage")!;
const startUpdateDownload = inject<() => void>("startUpdateDownload")!;
const steamApiKey = inject<Ref<string | null>>("steamApiKey")!;
const setupDeviceName = inject<Ref<string>>("setupDeviceName")!;
const reloadLibrary = inject<() => Promise<void>>("loadLibrary")!;

const toast = useToast();
const shaderCache = ref<CacheSummary | null>(null);
const pipelineCache = ref<CacheSummary | null>(null);
const apiKeyInput = ref("");
const gptkInstallMessage = ref("");

onMounted(async () => {
  apiKeyInput.value = steamApiKey.value ?? "";
  await refreshCacheSizes();
});

async function refreshCacheSizes() {
  const result = await api<{
    ok: boolean;
    shader_cache: CacheSummary;
    pipeline_cache: CacheSummary;
  }>("GET", "/cache/size");
  if (result?.ok) {
    shaderCache.value = result.shader_cache;
    pipelineCache.value = result.pipeline_cache;
  }
}

function formatBytes(bytes: number): string {
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
  if (bytes < 1024 * 1024 * 1024) return `${(bytes / (1024 * 1024)).toFixed(1)} MB`;
  return `${(bytes / (1024 * 1024 * 1024)).toFixed(1)} GB`;
}

async function saveApiKey() {
  const key = apiKeyInput.value.trim();
  if (!key) {
    toast.show("Please enter a Steam API key", "error");
    return;
  }
  await api("POST", "/steam/save-api-key", { key });
  toast.show("API key saved — syncing library...", "success");
  await reloadLibrary();
  steamApiKey.value = key;
}

async function changeDeviceName() {
  const result = await api<{ name: string }>("GET", "/setup/device-name");
  if (result?.name) {
    setupDeviceName.value = result.name;
    await api("POST", "/setup/save", { deviceName: result.name });
    toast.show(`Device name changed to ${result.name}`);
  }
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
      toast.show("Steam started", "success");
    } else {
      toast.show(result?.error ?? "Failed to start Steam", "error");
    }
  }
  await reloadLibrary();
}

async function installGptkSteam() {
  if (!gptkToolkitInstalled.value) {
    const installed = await openGptkToolkitDownload();
    if (!installed) return;
  }
  if (gptkSteamInstalling.value) {
    toast.show("GPTK Steam setup is already running", "success");
    return;
  }
  toast.show("Starting GPTK Steam setup...", "success");
  const result = await api<{ ok: boolean; installing?: boolean; installed?: boolean; error?: string }>(
    "POST",
    "/steam/gptk-install",
  );
  if (result?.ok) {
    gptkSteamInstalling.value = result.installing ?? false;
    gptkSteamInstalled.value = result.installed ?? gptkSteamInstalled.value;
    gptkInstallMessage.value = result.installed ? "GPTK Steam is ready" : "Steam installer is running in GPTK Wine";
    toast.show(result.installed ? "GPTK Steam is already installed" : "Steam installer launched in GPTK Wine", "success");
    if (result.installing) pollGptkSteamInstall();
    await reloadLibrary();
  } else {
    toast.show(result?.error ?? "Could not start GPTK Steam setup", "error");
  }
}

async function openGptkToolkitDownload() {
  const result = await api<{ ok: boolean; installed?: boolean; download_required?: boolean; url?: string; error?: string }>(
    "POST",
    "/steam/gptk-toolkit-install",
  );
  if (result?.ok && result.installed) {
    gptkToolkitInstalled.value = true;
    gptkInstallMessage.value = "Game Porting Toolkit runtime is installed";
    toast.show("Game Porting Toolkit runtime installed", "success");
    return true;
  } else if (result?.ok) {
    toast.show("Game Porting Toolkit download page opened", "success");
  } else {
    toast.show(result?.error ?? "Could not set up Game Porting Toolkit runtime", "error");
  }
  return false;
}

async function pollGptkSteamInstall() {
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
    gptkInstallMessage.value = status.gptk_install_progress?.message ?? "";
    if (status.gptk_steam_installed) {
      clearInterval(poll);
      gptkSteamInstalling.value = false;
      toast.show("GPTK Steam is ready", "success");
      await reloadLibrary();
    } else if (status.gptk_install_progress?.phase === "error") {
      clearInterval(poll);
      gptkSteamInstalling.value = false;
      toast.show(status.gptk_install_progress.error ?? status.gptk_install_progress.message, "error");
    } else if (attempts > 180) {
      clearInterval(poll);
      gptkSteamInstalling.value = false;
      toast.show("GPTK Steam setup is still running. Check the Steam installer window.", "error");
    }
  }, 2000);
}

async function toggleGptkSteam() {
  if (!gptkSteamInstalled.value) {
    await installGptkSteam();
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
    const result = await api<{ ok: boolean; running?: boolean; installed?: boolean; error?: string }>("POST", "/steam/gptk-launch");
    if (result?.ok) {
      gptkSteamInstalled.value = result.installed ?? gptkSteamInstalled.value;
      gptkSteamRunning.value = result.running ?? true;
      toast.show("GPTK Steam started", "success");
    } else {
      toast.show(result?.error ?? "Failed to start GPTK Steam", "error");
    }
  }
  await reloadLibrary();
}

async function installMacSteam() {
  const result = await api<{ ok: boolean; installed?: boolean; error?: string }>("POST", "/steam/mac-install");
  if (result?.ok) {
    if (result.installed) {
      macSteamInstalled.value = true;
      toast.show("macOS Steam is already installed", "success");
    } else {
      toast.show("Steam download page opened", "success");
    }
  } else {
    toast.show(result?.error ?? "Could not open macOS Steam installer", "error");
  }
}

async function toggleMacSteam() {
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
}

async function restartBackend() {
  toast.show("Restarting backend...", "success");
  const result = await getAPI().restartBackend();
  if (result.ok) toast.show("Backend restarted", "success");
  else toast.show(result.error ?? "Failed to restart", "error");
}

async function openMetalsharpFolder() {
  const result = await getAPI().openMetalsharpFolder();
  toast.show(
    result?.ok ? "MetalSharp data folder opened" : (result?.error ?? "Failed to open data folder"),
    result?.ok ? "success" : "error",
  );
}

async function openLogsFolder() {
  const result = await getAPI().openLogsFolder();
  toast.show(
    result?.ok ? "Logs folder opened" : (result?.error ?? "Failed to open logs"),
    result?.ok ? "success" : "error",
  );
}

async function repairDataAccess() {
  const result = await getAPI().repairDataAccess();
  const failed = result?.checks?.filter((check) => !check.ok) ?? [];
  if (result?.ok) {
    toast.show("MetalSharp data access verified", "success");
  } else {
    toast.show(failed[0]?.error ?? result?.error ?? "MetalSharp data access needs attention", "error");
  }
}

async function clearShaderCache() {
  const result = await api<{ ok: boolean; bytes_freed: number; files_removed: number }>("POST", "/cache/clear", {
    type: "shader",
  });
  if (result?.ok) toast.show(`Shader cache cleared — ${formatBytes(result.bytes_freed)} freed`);
  await refreshCacheSizes();
}

async function clearPipelineCache() {
  const result = await api<{ ok: boolean; bytes_freed: number; files_removed: number }>("POST", "/cache/clear", {
    type: "pipeline",
  });
  if (result?.ok) toast.show(`Pipeline cache cleared — ${formatBytes(result.bytes_freed)} freed`);
  await refreshCacheSizes();
}

async function checkForUpdates() {
  toast.show("Checking for updates...", "success");
  const result = await api<UpdateStatus>("GET", "/update/check");
  if (result) updateStatus.value = result;
  if (result?.ok && result.available) toast.show(`Update available: v${result.latest_version}`, "success");
  else if (result?.ok) toast.show("You're up to date!", "success");
  else toast.show("Could not check for updates", "error");
}

function cacheBadgeClass(cache: CacheSummary | null): string {
  if (!cache || cache.status === "missing" || cache.status === "empty") return "badge-warn";
  return "badge-ok";
}

function cacheStatusText(cache: CacheSummary | null): string {
  if (!cache) return "...";
  if (cache.status === "missing") return "Missing";
  if (cache.status === "empty") return "Empty";
  return `${formatBytes(cache.bytes)} · ${cache.files} files`;
}
</script>

<template>
  <div class="settings-view">
    <div class="settings-header"><h1>Settings</h1></div>

    <div class="settings-section">
      <h2>Steam Integration</h2>
      <div class="settings-row">
        <div>
          <div class="settings-label">Steam Web API Key</div>
          <div class="settings-desc">
            Required to load your full game library. Get a free key at
            <a href="https://steamcommunity.com/dev/apikey" target="_blank">steamcommunity.com/dev/apikey</a>.
          </div>
        </div>
        <div class="settings-value">
          <div class="settings-input-row">
            <input
              v-model="apiKeyInput"
              type="password"
              class="control-input"
              placeholder="Enter your Steam Web API key..."
            />
            <button class="btn btn-primary btn-sm" @click="saveApiKey">Save &amp; Sync</button>
          </div>
          <span v-if="steamApiKey" class="badge badge-ok">Key saved</span>
          <span v-else class="badge badge-warn">No key — only installed games shown</span>
        </div>
      </div>
      <div class="settings-row">
        <div>
          <div class="settings-label">Device Name</div>
          <div class="settings-desc">Identifies this machine to Steam for persistent login</div>
        </div>
        <div class="settings-value">
          <span>{{ setupDeviceName || "Not set" }}</span>
          <button class="btn btn-secondary btn-sm" @click="changeDeviceName">Change</button>
        </div>
      </div>
    </div>

    <div class="settings-section">
      <h2>Steam</h2>
      <div class="settings-row">
        <div>
          <div class="settings-label">Wine Steam (MetalSharp)</div>
          <div class="settings-desc">Windows Steam running in MetalSharp Wine</div>
        </div>
        <div class="settings-value">
          <span v-if="wineSteamInstalled" class="badge badge-ok">Installed</span>
          <span v-else class="badge badge-warn">Not Installed</span>
          <button v-if="wineSteamInstalled" class="btn btn-secondary btn-sm" @click="toggleSteam">
            {{ wineSteamRunning ? "Stop Steam" : "Start Steam" }}
          </button>
        </div>
      </div>
      <div class="settings-row">
        <div>
          <div class="settings-label">GPTK Steam (D3DMetal)</div>
          <div class="settings-desc">Separate Windows Steam install inside Game Porting Toolkit Wine</div>
          <div v-if="gptkInstallMessage" class="settings-desc">{{ gptkInstallMessage }}</div>
        </div>
        <div class="settings-value">
          <span v-if="!gptkToolkitInstalled" class="badge badge-warn">GPTK Missing</span>
          <span v-else-if="gptkSteamInstalling" class="badge badge-warn">Installing</span>
          <span v-else-if="gptkSteamInstalled" class="badge badge-ok">Installed</span>
          <span v-else class="badge badge-warn">Not Installed</span>
          <button
            v-if="gptkToolkitInstalled"
            class="btn btn-secondary btn-sm"
            :disabled="gptkSteamInstalling"
            @click="gptkSteamInstalled ? toggleGptkSteam() : installGptkSteam()"
          >
            {{
              gptkSteamInstalling
                ? "Installing..."
                : gptkSteamInstalled
                  ? gptkSteamRunning
                    ? "Stop GPTK Steam"
                    : "Start GPTK Steam"
              : "Install GPTK Steam"
            }}
          </button>
          <button v-else class="btn btn-primary btn-sm" @click="openGptkToolkitDownload">Get GPTK</button>
        </div>
      </div>
      <div class="settings-row">
        <div>
          <div class="settings-label">Steam Mac</div>
          <div class="settings-desc">Native macOS Steam used for games with Mac builds</div>
        </div>
        <div class="settings-value">
          <span v-if="macSteamInstalled" class="badge badge-ok">Installed</span>
          <span v-else class="badge badge-warn">Not Installed</span>
          <button v-if="macSteamInstalled" class="btn btn-secondary btn-sm" @click="toggleMacSteam">
            {{ macSteamRunning ? "Stop Steam Mac" : "Start Steam Mac" }}
          </button>
          <button v-else class="btn btn-primary btn-sm" @click="installMacSteam">Install macOS Steam</button>
        </div>
      </div>
    </div>

    <div class="settings-section">
      <h2>Backend</h2>
      <div class="settings-row">
        <div>
          <div class="settings-label">Game Runtime Backend</div>
          <div class="settings-desc">
            The Rust backend handles game launches, Steam integration, and shader management
          </div>
        </div>
        <div class="settings-value">
          <span class="badge" :class="backendConnected ? 'badge-ok' : 'badge-warn'">
            {{ backendConnected ? "Connected" : "Offline" }}
          </span>
          <span v-if="backendVersion" class="settings-version">v{{ backendVersion }}</span>
        </div>
      </div>
      <div class="settings-row">
        <div>
          <div class="settings-label">Restart Backend</div>
          <div class="settings-desc">Kill and restart the backend process</div>
        </div>
        <div class="settings-value">
          <button class="btn btn-secondary btn-sm" @click="restartBackend">Restart Backend</button>
        </div>
      </div>
    </div>

    <div class="settings-section">
      <h2>Data &amp; Permissions</h2>
      <div class="settings-row">
        <div>
          <div class="settings-label">MetalSharp Data Folder</div>
          <div class="settings-desc">
            Preserves logs, Sharp Library apps, covers, launch options, caches, and runtime state across updates.
          </div>
        </div>
        <div class="settings-value">
          <button class="btn btn-secondary btn-sm" @click="openMetalsharpFolder">Open Data</button>
          <button class="btn btn-secondary btn-sm" @click="openLogsFolder">Open Logs</button>
        </div>
      </div>
      <div class="settings-row">
        <div>
          <div class="settings-label">Data Access Check</div>
          <div class="settings-desc">
            Recreates required folders and verifies MetalSharp can write logs and library metadata after an app update.
          </div>
        </div>
        <div class="settings-value">
          <button class="btn btn-primary btn-sm" @click="repairDataAccess">Repair &amp; Verify</button>
        </div>
      </div>
    </div>

    <div class="settings-section">
      <h2>Cache</h2>
      <div class="settings-row">
        <div>
          <div class="settings-label">Shader Cache</div>
          <div class="settings-desc">Persist compiled shaders to disk for faster loading</div>
        </div>
        <div class="settings-value">
          <span class="badge" :class="cacheBadgeClass(shaderCache)">{{ cacheStatusText(shaderCache) }}</span>
          <span v-if="shaderCache?.apps" class="settings-version">{{ shaderCache.apps }} apps</span>
          <span v-if="shaderCache?.last_modified" class="settings-version">{{ shaderCache.last_modified }}</span>
          <button class="btn btn-secondary btn-sm" @click="clearShaderCache">Clear</button>
        </div>
      </div>
      <div class="settings-row">
        <div>
          <div class="settings-label">Pipeline Cache</div>
          <div class="settings-desc">Persist compiled pipeline state objects</div>
        </div>
        <div class="settings-value">
          <span class="badge" :class="cacheBadgeClass(pipelineCache)">{{ cacheStatusText(pipelineCache) }}</span>
          <span v-if="pipelineCache?.last_modified" class="settings-version">{{ pipelineCache.last_modified }}</span>
          <button class="btn btn-secondary btn-sm" @click="clearPipelineCache">Clear</button>
        </div>
      </div>
    </div>

    <div class="settings-section">
      <h2>Updates</h2>
      <div class="settings-row">
        <div>
          <div class="settings-label">Version</div>
          <div class="settings-desc">
            {{
              updateStatus?.ok && updateStatus?.available
                ? `v${updateStatus.latest_version} available (current: v${updateStatus.current_version})`
                : updateStatus?.ok
                  ? "You're up to date"
                  : "Could not check for updates"
            }}
          </div>
        </div>
        <div class="settings-value">
          <span class="badge" :class="updateStatus?.ok ? 'badge-ok' : 'badge-warn'">
            v{{ updateStatus?.current_version ?? "unknown" }}
          </span>
          <button v-if="!updateDownloading" class="btn btn-secondary btn-sm" @click="checkForUpdates">Check Now</button>
        </div>
      </div>
      <div v-if="updateStatus?.ok && updateStatus?.available && !updateDownloading" class="settings-row">
        <div>
          <div class="settings-label">Download Update</div>
          <div class="settings-desc">v{{ updateStatus.latest_version }} is ready to download</div>
        </div>
        <div class="settings-value">
          <button class="btn btn-primary btn-sm" @click="startUpdateDownload">Download &amp; Install</button>
        </div>
      </div>
      <div v-if="updateDownloading" class="settings-row">
        <div>
          <div class="settings-label">{{ updateMessage || "Updating..." }}</div>
          <div class="settings-desc">Do not close MetalSharp during the update</div>
        </div>
        <div class="settings-value">
          <div class="update-progress-bar">
            <div class="update-progress-fill" :style="{ width: updateProgress + '%' }"></div>
          </div>
          <span class="settings-version">{{ updateProgress }}%</span>
        </div>
      </div>
    </div>
  </div>
</template>

<style scoped>
.settings-view {
  padding: 24px 28px;
  height: 100%;
  overflow-y: auto;
}
.settings-header {
  margin: -24px -28px 24px;
  padding: 24px 28px 18px;
  background: var(--page-header-bg);
  border-bottom: 1px solid var(--border);
}
.settings-header h1 {
  font-size: 22px;
  font-weight: 600;
}

.settings-section {
  margin-bottom: 28px;
  padding-bottom: 20px;
  border-bottom: 1px solid var(--border);
}
.settings-section:last-child {
  border-bottom: none;
}
.settings-section h2 {
  font-size: 14px;
  font-weight: 600;
  color: var(--accent);
  margin-bottom: 14px;
  text-transform: uppercase;
  letter-spacing: 0.5px;
}

.settings-row {
  display: flex;
  justify-content: space-between;
  align-items: flex-start;
  gap: 24px;
  padding: 10px 0;
}
.settings-label {
  font-size: 13px;
  font-weight: 600;
  margin-bottom: 2px;
}
.settings-desc {
  font-size: 12px;
  color: var(--text-dim);
  line-height: 1.4;
}
.settings-value {
  display: flex;
  align-items: center;
  gap: 8px;
  flex-shrink: 0;
  flex-wrap: wrap;
  justify-content: flex-end;
}
.settings-input-row {
  display: flex;
  gap: 8px;
  align-items: center;
}
.settings-input-row input {
  width: 280px;
}
.settings-version {
  font-size: 12px;
  color: var(--text-dim);
}
.update-progress-bar {
  width: 140px;
  height: 6px;
  background: var(--border);
  border-radius: 3px;
  overflow: hidden;
}
.update-progress-fill {
  height: 100%;
  background: var(--accent);
  border-radius: 3px;
  transition: width 0.3s ease;
}
</style>
