<script setup lang="ts">
import { computed, ref, onMounted } from "vue";
import { useToast } from "../composables/useToast";
import { api } from "../composables/useApi";

interface SteamGame {
  appid: number;
  name: string;
  installed: boolean;
  state: "installed" | "not_installed" | "downloading";
  cover_url: string;
  header_url: string;
  size_bytes?: number | null;
  launch_method?: string;
  available_pipelines?: PipelineOption[];
  has_native_build?: boolean;
  can_uninstall?: boolean;
}

interface PipelineOption {
  id: string;
  name: string;
  description?: string;
  recommended?: boolean;
}

interface LaunchDoctorCheck {
  id: string;
  label: string;
  ok: boolean;
  detail: string;
}

interface LaunchDoctorReport {
  ready: boolean;
  summary: string;
  blockers: string[];
  warnings: string[];
  checks: LaunchDoctorCheck[];
  recipe: {
    pipeline: string;
    pipeline_name: string;
    backend: string;
    exe_name?: string | null;
  };
}

const props = defineProps<{
  game: SteamGame;
  running: boolean;
  launching: boolean;
  steamInstalled: boolean;
}>();

const emit = defineEmits<{
  play: [launchMethod: string];
  stop: [];
  install: [];
  uninstall: [];
}>();

const toast = useToast();
const goldbergActive = ref(false);
const eacActive = ref(false);
const pipelineName = ref("Auto");
const selectedLaunchMode = ref("auto");
const pipelineOptions = ref<PipelineOption[]>([]);
const doctorOpen = ref(false);
const doctorLoading = ref(false);
const doctorReport = ref<LaunchDoctorReport | null>(null);

const launchModeOptions = computed(() => {
  const byId = new Map<string, PipelineOption>();
  byId.set("auto", { id: "auto", name: `Auto${pipelineName.value !== "Auto" ? ` (${pipelineName.value})` : ""}` });
  for (const option of pipelineOptions.value) {
    if (option.id === "mac_steam" && !props.game.has_native_build) continue;
    byId.set(option.id, option);
  }
  for (const option of props.game.available_pipelines ?? []) {
    if (option.id === "mac_steam" && !props.game.has_native_build) continue;
    byId.set(option.id, option);
  }
  if (props.game.has_native_build) byId.set("mac_steam", { id: "mac_steam", name: "MacOS Steam" });
  return [...byId.values()];
});

onMounted(async () => {
  if (props.game.installed) {
    const gp = await api<{ ok: boolean; recommended: string; recommended_name: string; pipelines: PipelineOption[] }>(
      "GET",
      `/mtsp/pipelines?appid=${props.game.appid}`,
    );
    if (gp?.ok && gp.recommended_name) {
      pipelineName.value = gp.recommended_name;
      pipelineOptions.value = gp.pipelines ?? [];
    }

    const gs = await api<{ ok: boolean; goldberg_active: boolean }>(
      "GET",
      `/goldberg/status?appid=${props.game.appid}`,
    );
    if (gs?.ok) goldbergActive.value = gs.goldberg_active;

    const es = await api<{ ok: boolean; eac_toggle_active: boolean }>(
      "GET",
      `/eac-toggle/status?appid=${props.game.appid}`,
    );
    if (es?.ok) eacActive.value = es.eac_toggle_active;
  }
});

async function toggleGoldberg(enable: boolean) {
  const result = await api<{ ok: boolean; goldberg_active: boolean }>("POST", "/goldberg/toggle", {
    appid: props.game.appid,
    enable,
  });
  if (result?.ok) {
    goldbergActive.value = result.goldberg_active;
    toast.show(enable ? "Goldberg enabled" : "Goldberg disabled", "success");
  } else {
    toast.show("Failed to toggle Goldberg", "error");
  }
}

async function toggleEac(enable: boolean) {
  const result = await api<{ ok: boolean; eac_toggle_active: boolean }>("POST", "/eac-toggle/toggle", {
    appid: props.game.appid,
    enable,
  });
  if (result?.ok) {
    eacActive.value = result.eac_toggle_active;
    toast.show(enable ? "EAC bypass enabled (offline only)" : "EAC bypass disabled", "success");
  } else {
    toast.show("Failed to toggle EAC bypass", "error");
  }
}

async function runDoctor() {
  doctorOpen.value = true;
  doctorLoading.value = true;
  doctorReport.value = null;
  const result = await api<{ ok: boolean; report?: LaunchDoctorReport; error?: string }>("POST", "/mtsp/doctor", {
    appid: props.game.appid,
    launchMethod: selectedLaunchMode.value,
  });
  doctorLoading.value = false;

  if (result?.ok && result.report) {
    doctorReport.value = result.report;
  } else {
    toast.show(result?.error ?? "Launch Doctor failed", "error");
  }
}

function formatBytes(bytes: number): string {
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
  if (bytes < 1024 * 1024 * 1024) return `${(bytes / (1024 * 1024)).toFixed(1)} MB`;
  return `${(bytes / (1024 * 1024 * 1024)).toFixed(1)} GB`;
}
</script>

<template>
  <div class="game-card" :class="{ running }">
    <div class="game-card-banner">
      <img
        v-if="game.header_url || game.cover_url"
        :src="game.header_url || game.cover_url"
        :alt="game.name"
        class="game-card-cover"
        loading="lazy"
        @error="($event.target as HTMLImageElement).style.display = 'none'"
      />
      <span v-else class="game-icon-placeholder">{{ game.name.charAt(0).toUpperCase() }}</span>
    </div>
    <div class="game-card-body">
      <div class="game-card-title">{{ game.name }}</div>
      <div class="game-card-meta">
        <span class="badge" :class="game.installed ? 'badge-ok' : 'badge-warn'">
          {{ game.installed ? "Installed" : "Not Installed" }}
        </span>
        <span v-if="game.size_bytes" class="game-card-size">{{ formatBytes(game.size_bytes) }}</span>
      </div>
      <div class="game-card-actions">
        <div v-if="launching" class="launching-indicator">
          <div class="spinner"></div>
          <span>Preparing runtime and launching...</span>
        </div>
        <div v-else-if="running" class="game-card-actions-stack">
          <button class="btn btn-stop" @click="emit('stop')">Stop</button>
          <span class="badge badge-ok" style="font-size:10px;padding:2px 8px;">{{ pipelineName }}</span>
        </div>
        <div v-else-if="game.installed" class="game-card-actions-stack">
          <div class="game-card-actions-row">
            <button class="btn btn-play" @click="emit('play', selectedLaunchMode)">Play</button>
            <select v-model="selectedLaunchMode" class="launch-mode-select" title="Launch mode">
              <option v-for="option in launchModeOptions" :key="option.id" :value="option.id">
                {{ option.name }}
              </option>
            </select>
            <label class="toggle-label" title="Goldberg Steam emulator">
              <input type="checkbox" :checked="goldbergActive" @change="toggleGoldberg(($event.target as HTMLInputElement).checked)" />
              <span class="toggle-switch"></span>
              <span class="toggle-text">Goldberg</span>
            </label>
            <label class="toggle-label" title="EAC bypass (offline only)">
              <input type="checkbox" :checked="eacActive" @change="toggleEac(($event.target as HTMLInputElement).checked)" />
              <span class="toggle-switch"></span>
              <span class="toggle-text">No EAC</span>
            </label>
            <button class="btn btn-secondary btn-sm" :disabled="doctorLoading" @click="runDoctor">
              {{ doctorLoading ? "Checking" : "Doctor" }}
            </button>
          </div>
          <div class="game-card-actions-row subtle">
            <span class="badge badge-ok" style="font-size:10px;padding:2px 8px;">{{ pipelineName }}</span>
            <button v-if="game.can_uninstall !== false" class="btn btn-danger btn-sm" @click="emit('uninstall')">Uninstall</button>
          </div>
          <div v-if="doctorOpen" class="doctor-panel">
            <div v-if="doctorLoading" class="doctor-loading">Checking launch prerequisites...</div>
            <template v-else-if="doctorReport">
              <div class="doctor-summary">
                <span class="badge" :class="doctorReport.ready ? 'badge-ok' : 'badge-warn'">
                  {{ doctorReport.ready ? "Ready" : "Blocked" }}
                </span>
                <span>{{ doctorReport.summary }}</span>
              </div>
              <div class="doctor-checks">
                <div
                  v-for="check in doctorReport.checks"
                  :key="check.id"
                  class="doctor-check"
                  :class="{ failed: !check.ok }"
                >
                  <span class="doctor-check-state">{{ check.ok ? "OK" : "!" }}</span>
                  <span class="doctor-check-label">{{ check.label }}</span>
                  <span class="doctor-check-detail">{{ check.detail }}</span>
                </div>
              </div>
              <div v-if="doctorReport.blockers.length" class="doctor-notes blocked">
                <div v-for="blocker in doctorReport.blockers" :key="blocker">{{ blocker }}</div>
              </div>
              <div v-if="doctorReport.warnings.length" class="doctor-notes">
                <div v-for="warning in doctorReport.warnings" :key="warning">{{ warning }}</div>
              </div>
            </template>
          </div>
        </div>
        <div v-else-if="steamInstalled" class="game-card-actions-stack">
          <button class="btn btn-install" @click="emit('install')">Install</button>
        </div>
        <span v-else class="badge badge-warn">Setup Steam</span>
      </div>
    </div>
  </div>
</template>

<style scoped>
.game-card {
  background: var(--game-card-bg, var(--bg-card));
  border: 1px solid var(--border);
  border-radius: var(--radius-lg);
  overflow: hidden;
  transition: border-color var(--transition), box-shadow var(--transition);
  box-shadow: 0 0 18px var(--card-glow);
}
.game-card:hover {
  border-color: var(--border-strong);
  box-shadow: 0 4px 20px rgba(0, 0, 0, 0.15);
}
.game-card.running {
  border-color: var(--success);
}

.game-card-banner {
  width: 100%;
  height: 140px;
  background: var(--bg-surface);
  display: flex;
  align-items: center;
  justify-content: center;
  overflow: hidden;
}
.game-card-cover {
  width: 100%;
  height: 100%;
  object-fit: cover;
}
.game-icon-placeholder {
  font-size: 36px;
  font-weight: 700;
  color: var(--text-dim);
  opacity: 0.4;
}

.game-card-body {
  background: var(--game-card-body-bg, transparent);
  color: var(--game-card-text, var(--text-primary));
  padding: 12px 14px 14px;
}
.game-card-title {
  font-size: 14px;
  font-weight: 600;
  margin-bottom: 6px;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}
.game-card-meta {
  display: flex;
  align-items: center;
  gap: 6px;
  margin-bottom: 10px;
  flex-wrap: wrap;
}
.game-card-size {
  font-size: 11px;
  color: var(--game-card-dim, var(--text-dim));
}

.game-card-actions {
  min-height: 40px;
}
.game-card-actions-stack {
  display: flex;
  flex-direction: column;
  gap: 8px;
}
.game-card-actions-row {
  display: flex;
  align-items: center;
  gap: 8px;
  flex-wrap: wrap;
}
.launch-mode-select {
  min-width: 118px;
  max-width: 150px;
  height: 30px;
  background: var(--bg-input);
  color: var(--text-primary);
  border: 1px solid var(--border-strong);
  border-radius: var(--radius-sm);
  padding: 4px 8px;
  font-size: 12px;
  outline: none;
}
.game-card-actions-row.subtle {
  opacity: 0.7;
}

.launching-indicator {
  display: flex;
  align-items: center;
  gap: 8px;
  font-size: 12px;
  color: var(--text-dim);
}

.doctor-panel {
  border: 1px solid var(--border);
  border-radius: var(--radius-sm);
  background: var(--bg-surface);
  padding: 10px;
  display: flex;
  flex-direction: column;
  gap: 8px;
}
.doctor-loading,
.doctor-summary,
.doctor-check {
  font-size: 11px;
  color: var(--text-secondary);
}
.doctor-summary {
  display: flex;
  align-items: center;
  gap: 8px;
  flex-wrap: wrap;
}
.doctor-checks {
  display: flex;
  flex-direction: column;
  gap: 4px;
}
.doctor-check {
  display: grid;
  grid-template-columns: 28px 82px minmax(0, 1fr);
  gap: 6px;
  align-items: start;
}
.doctor-check.failed .doctor-check-state,
.doctor-notes.blocked {
  color: var(--error);
}
.doctor-check-state {
  color: var(--success);
  font-weight: 700;
}
.doctor-check-label {
  color: var(--text-primary);
}
.doctor-check-detail {
  min-width: 0;
  overflow-wrap: anywhere;
  color: var(--text-dim);
}
.doctor-notes {
  display: flex;
  flex-direction: column;
  gap: 4px;
  font-size: 11px;
  color: var(--text-secondary);
  overflow-wrap: anywhere;
}

@media (max-width: 720px) {
  .doctor-check {
    grid-template-columns: 28px minmax(0, 1fr);
  }
  .doctor-check-detail {
    grid-column: 2;
  }
}
</style>
