<script setup lang="ts">
import { computed, ref, onMounted, watch } from "vue";
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
  launch_method_name?: string;
  preferred_pipeline?: string | null;
  available_pipelines?: PipelineOption[];
  has_native_build?: boolean;
  can_uninstall?: boolean;
  bottle_id?: string | null;
  bottle_health?: string | null;
  bottle_runtime_assets?: number;
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

interface SteamRuntimeReport {
  appid?: number | null;
  bottle_id?: string | null;
  bottle_name?: string | null;
  preferred_pipeline?: string | null;
  pipeline: string;
  runtime_profile: string;
  prefix_path: string;
  game_install_path?: string | null;
  runtime_assets: { id: string; kind: string; source_path: string; present: boolean }[];
  components: { id: string; state: string }[];
  actions: { id: string; status: string; detail: string }[];
}

interface ComponentRepair {
  id: string;
  status: string;
  detail: string;
  pid?: number | null;
}

interface BottleEditResponse {
  ok: boolean;
  bottle?: {
    id: string;
    name: string;
    custom_name?: string | null;
    preferred_pipeline?: string | null;
  };
  preflight?: {
    ok: boolean;
    deployed_dlls?: number;
    error?: string;
  };
  error?: string;
}

const props = defineProps<{
  game: SteamGame;
  running: boolean;
  launching: boolean;
  steamInstalled: boolean;
  developerMode: boolean;
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
const pipelineResolvedLocally = ref(false);
const selectedLaunchMode = ref("auto");
const pipelineOptions = ref<PipelineOption[]>([]);
const doctorOpen = ref(false);
const doctorLoading = ref(false);
const doctorReport = ref<LaunchDoctorReport | null>(null);
const runtimeOpen = ref(false);
const runtimeLoading = ref(false);
const runtimeReport = ref<SteamRuntimeReport | null>(null);
const bottleName = ref("");
const bottlePreferredMode = ref("auto");
const bottleSaving = ref(false);
const launchModeStorageKey = computed(() => `metalsharp-launch-mode-${props.game.appid}`);
const userSelectablePipelineOrder = ["d3dmetal", "m12", "m11", "m10", "m9", "fna_arm64"];
const userSelectablePipelineNames: Record<string, string> = {
  m12: "M12",
  d3dmetal: "D3DMetal",
  m11: "M11",
  m10: "M10",
  m9: "M9",
  fna_arm64: "Mono/FNA",
};

function normalizePipelineOption(option: PipelineOption): PipelineOption | null {
  const id = option.id.toLowerCase();
  if (!userSelectablePipelineOrder.includes(id)) return null;
  return {
    ...option,
    id,
    name: userSelectablePipelineNames[id] ?? option.name,
  };
}

const launchModeOptions = computed(() => {
  const byId = new Map<string, PipelineOption>();
  byId.set("auto", { id: "auto", name: `Auto${pipelineName.value !== "Auto" ? ` (${pipelineName.value})` : ""}` });
  for (const option of pipelineOptions.value) {
    const normalized = normalizePipelineOption(option);
    if (normalized) byId.set(normalized.id, normalized);
  }
  for (const option of props.game.available_pipelines ?? []) {
    const normalized = normalizePipelineOption(option);
    if (normalized) byId.set(normalized.id, normalized);
  }
  return [...byId.values()];
});

const bottlePipelineOptions = computed(() =>
  userSelectablePipelineOrder.map((id) => ({ id, name: userSelectablePipelineNames[id] })),
);

function preferredBottlePipeline(report: SteamRuntimeReport) {
  const candidates = [report.preferred_pipeline, report.pipeline, props.game.preferred_pipeline, props.game.launch_method];
  return candidates.find((id) => id && userSelectablePipelineOrder.includes(id)) ?? "m11";
}

function runtimeDoctorPipelineRequest() {
  return selectedLaunchMode.value === "auto" ? "auto" : selectedLaunchMode.value;
}

onMounted(async () => {
  const savedLaunchMode = localStorage.getItem(launchModeStorageKey.value);
  if (savedLaunchMode) selectedLaunchMode.value = savedLaunchMode;

  if (props.game.installed) {
    await refreshPipelineMetadata();
    if (selectedLaunchMode.value !== "auto" && !userSelectablePipelineOrder.includes(selectedLaunchMode.value)) {
      selectedLaunchMode.value = "auto";
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

watch(selectedLaunchMode, (mode) => {
  localStorage.setItem(launchModeStorageKey.value, mode);
});

watch(
  () => [props.game.launch_method, props.game.launch_method_name, props.game.preferred_pipeline],
  () => {
    if (pipelineResolvedLocally.value) return;
    const routeId = props.game.preferred_pipeline || props.game.launch_method;
    if (routeId && userSelectablePipelineOrder.includes(routeId)) {
      pipelineName.value = props.game.launch_method_name || userSelectablePipelineNames[routeId] || pipelineName.value;
    }
  },
);

async function refreshPipelineMetadata() {
  const gp = await api<{
    ok: boolean;
    recommended: string;
    recommended_name: string;
    preferred?: string | null;
    preferred_name?: string | null;
    pipelines: PipelineOption[];
  }>("GET", `/mtsp/pipelines?appid=${props.game.appid}`);
  if (gp?.ok && gp.recommended_name) {
    pipelineName.value = gp.preferred_name || gp.recommended_name;
    pipelineResolvedLocally.value = true;
    pipelineOptions.value = gp.pipelines ?? [];
  }
}

async function toggleGoldberg(enable: boolean) {
  const result = await api<{ ok: boolean; goldberg_active: boolean }>("POST", "/goldberg/toggle", {
    appid: props.game.appid,
    enable,
  });
  if (result?.ok) {
    goldbergActive.value = result.goldberg_active;
    toast.show(enable ? "Steam Emu enabled" : "Steam Emu disabled", "success");
  } else {
    toast.show("Failed to toggle Steam Emu", "error");
  }
}

async function toggleEac(enable: boolean) {
  const result = await api<{ ok: boolean; eac_toggle_active: boolean }>("POST", "/eac-toggle/toggle", {
    appid: props.game.appid,
    enable,
  });
  if (result?.ok) {
    eacActive.value = result.eac_toggle_active;
    toast.show(enable ? "Offline EAC mode enabled" : "Offline EAC mode disabled", "success");
  } else {
    toast.show("Failed to toggle offline EAC mode", "error");
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

async function runRuntimeDoctor() {
  runtimeOpen.value = true;
  runtimeLoading.value = true;
  runtimeReport.value = null;
  const result = await api<{ ok: boolean; report?: SteamRuntimeReport; error?: string }>(
    "POST",
    "/steam/runtime-doctor",
    {
      appid: props.game.appid,
      pipeline: runtimeDoctorPipelineRequest(),
    },
  );
  runtimeLoading.value = false;

  if (result?.ok && result.report) {
    runtimeReport.value = result.report;
    bottleName.value = result.report.bottle_name || props.game.name;
    bottlePreferredMode.value = preferredBottlePipeline(result.report);
  } else {
    toast.show(result?.error ?? "Runtime Doctor failed", "error");
  }
}

async function openBottleWorkspace() {
  if (runtimeOpen.value && runtimeReport.value) {
    runtimeOpen.value = false;
    return;
  }
  await runRuntimeDoctor();
}

async function repairRuntimeComponent(component: string) {
  if (!runtimeReport.value?.bottle_id) {
    toast.show("No Steam bottle is attached to this install yet", "error");
    return;
  }
  runtimeLoading.value = true;
  const result = await api<{ ok: boolean; repair?: ComponentRepair; error?: string }>(
    "POST",
    "/bottles/repair-component",
    {
      id: runtimeReport.value.bottle_id,
      component,
    },
  );
  if (result?.ok && result.repair) {
    toast.show(`${component}: ${result.repair.status}`, result.repair.status === "asset_missing" ? "error" : "success");
    if (result.repair.status === "started" || result.repair.status === "seeding") {
      await pollRuntimeRepairDone(runtimeReport.value.bottle_id, component);
    } else {
      await runRuntimeDoctor();
      runtimeLoading.value = false;
    }
  } else {
    toast.show(result?.error ?? "Runtime repair failed", "error");
    runtimeLoading.value = false;
  }
}

async function pollRuntimeRepairDone(bottleId: string, component: string) {
  const pollInterval = 5000;
  const maxPolls = 120;
  for (let i = 0; i < maxPolls; i++) {
    await new Promise((r) => setTimeout(r, pollInterval));
    const poll = await api<{ ok: boolean; repair?: ComponentRepair; error?: string }>(
      "POST",
      "/bottles/repair-component",
      {
        id: bottleId,
        component,
        dryRun: true,
      },
    );
    if (!poll?.ok || !poll.repair) break;
    const status = poll.repair.status;
    if (status === "already_installed" || status === "repair_available") {
      toast.show(`${component}: ${status === "already_installed" ? "ready" : status}`, "success");
      await runRuntimeDoctor();
      runtimeLoading.value = false;
      return;
    }
  }
  toast.show(`${component}: seeding is taking longer than expected — check back`, "info");
  await runRuntimeDoctor();
  runtimeLoading.value = false;
}

async function saveBottleEdit() {
  const bottleId = runtimeReport.value?.bottle_id;
  if (!bottleId) {
    toast.show("No Steam bottle is attached to this install yet", "error");
    return;
  }
  bottleSaving.value = true;
  const result = await api<BottleEditResponse>("POST", "/bottles/edit", {
    id: bottleId,
    name: bottleName.value,
    preferredPipeline: bottlePreferredMode.value,
  });
  bottleSaving.value = false;

  if (result?.ok && result.bottle) {
    bottleName.value = result.bottle.name;
    bottlePreferredMode.value = result.bottle.preferred_pipeline && userSelectablePipelineOrder.includes(result.bottle.preferred_pipeline)
      ? result.bottle.preferred_pipeline
      : bottlePreferredMode.value;
    selectedLaunchMode.value = bottlePreferredMode.value;
    pipelineName.value = userSelectablePipelineNames[bottlePreferredMode.value] || pipelineName.value;
    pipelineResolvedLocally.value = true;
    if (runtimeReport.value) {
      runtimeReport.value.bottle_name = result.bottle.name;
      runtimeReport.value.preferred_pipeline = result.bottle.preferred_pipeline || null;
    }
    if (result.preflight?.ok === false) {
      toast.show(result.preflight.error ?? "Bottle saved; runtime doctor needs attention", "error");
    } else {
      toast.show("Bottle settings saved", "success");
    }
    await refreshPipelineMetadata();
    await runRuntimeDoctor();
  } else {
    toast.show(result?.error ?? "Bottle settings failed", "error");
  }
}

function resetBottleName() {
  bottleName.value = "";
}

function formatBytes(bytes: number): string {
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
  if (bytes < 1024 * 1024 * 1024) return `${(bytes / (1024 * 1024)).toFixed(1)} MB`;
  return `${(bytes / (1024 * 1024 * 1024)).toFixed(1)} GB`;
}
</script>

<template>
  <div class="game-card" :class="{ running, installed: game.installed, uninstalled: !game.installed }">
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
      <button v-if="running" class="running-close-button" title="Close game" @click="emit('stop')">
        <svg
          width="14"
          height="14"
          viewBox="0 0 24 24"
          fill="none"
          stroke="currentColor"
          stroke-width="2.4"
          stroke-linecap="round"
        >
          <path d="M18 6 6 18" />
          <path d="m6 6 12 12" />
        </svg>
      </button>
    </div>
    <div class="game-card-body">
      <div class="game-card-heading">
        <div class="game-card-title">{{ game.name }}</div>
        <span class="badge" :class="game.installed ? 'badge-ok' : 'badge-warn'">
          {{ game.installed ? "Installed" : "Not Installed" }}
        </span>
      </div>
      <div class="game-card-meta">
        <span v-if="game.installed" class="route-chip">{{ pipelineName }}</span>

        <label v-if="game.installed" class="tool-chip toggle-label steam-emu-toggle" title="gbe_fork Steam emulator">
          <input
            type="checkbox"
            :checked="goldbergActive"
            @change="toggleGoldberg(($event.target as HTMLInputElement).checked)"
          />
          <span class="toggle-switch"></span>
          <span class="toggle-text">Steam Emu</span>
        </label>
        <span v-if="game.bottle_runtime_assets" class="game-card-size">{{ game.bottle_runtime_assets }} assets</span>
        <span v-if="game.size_bytes" class="game-card-size">{{ formatBytes(game.size_bytes) }}</span>
      </div>
      <div class="game-card-actions">
        <div v-if="launching" class="launching-indicator">
          <div class="spinner"></div>
          <span>Preparing runtime and launching...</span>
        </div>
        <div v-else-if="running" class="game-card-actions-stack">
          <button class="btn btn-stop" @click="emit('stop')">Stop</button>
          <span class="route-chip">{{ pipelineName }}</span>
        </div>
        <div v-else-if="game.installed" class="game-card-actions-stack">
          <div class="primary-action-row">
            <button class="icon-button bottle-button" title="Bottle" @click="openBottleWorkspace">
              <svg
                v-if="!runtimeLoading"
                width="17"
                height="17"
                viewBox="0 0 24 24"
                fill="none"
                stroke="currentColor"
                stroke-width="2"
                stroke-linecap="round"
                stroke-linejoin="round"
              >
                <path d="M10 2h4" />
                <path d="M11 2v5l-4 5v8a2 2 0 0 0 2 2h6a2 2 0 0 0 2-2v-8l-4-5V2" />
                <path d="M8 15h8" />
              </svg>
              <span v-else class="spinner"></span>
            </button>
            <button class="btn btn-play" @click="emit('play', selectedLaunchMode)">Play</button>
            <select v-if="developerMode" v-model="selectedLaunchMode" class="launch-mode-select" title="Launch mode">
              <option v-for="option in launchModeOptions" :key="option.id" :value="option.id">
                {{ option.name }}
              </option>
            </select>
            <button
              v-if="developerMode"
              class="icon-button doctor-button"
              :disabled="doctorLoading"
              title="Run Launch Doctor"
              @click="runDoctor"
            >
              <svg
                v-if="!doctorLoading"
                width="16"
                height="16"
                viewBox="0 0 24 24"
                fill="none"
                stroke="currentColor"
                stroke-width="2"
                stroke-linecap="round"
                stroke-linejoin="round"
              >
                <path d="M12 22s8-4 8-10V5l-8-3-8 3v7c0 6 8 10 8 10z" />
                <path d="M9 12h6" />
                <path d="M12 9v6" />
              </svg>
              <span v-else class="spinner"></span>
            </button>
            <button
              v-if="developerMode"
              class="icon-button doctor-button"
              :disabled="runtimeLoading"
              title="Run Runtime Doctor"
              @click="runRuntimeDoctor"
            >
              <svg
                v-if="!runtimeLoading"
                width="16"
                height="16"
                viewBox="0 0 24 24"
                fill="none"
                stroke="currentColor"
                stroke-width="2"
                stroke-linecap="round"
                stroke-linejoin="round"
              >
                <path
                  d="M21 16V8a2 2 0 0 0-1-1.73l-7-4a2 2 0 0 0-2 0l-7 4A2 2 0 0 0 3 8v8a2 2 0 0 0 1 1.73l7 4a2 2 0 0 0 2 0l7-4A2 2 0 0 0 21 16z"
                />
                <path d="M3.3 7 12 12l8.7-5" />
                <path d="M12 22V12" />
              </svg>
              <span v-else class="spinner"></span>
            </button>
          </div>
          <div v-if="developerMode" class="secondary-action-grid">
            <label class="tool-chip toggle-label" title="Offline EAC mode">
              <input
                type="checkbox"
                :checked="eacActive"
                @change="toggleEac(($event.target as HTMLInputElement).checked)"
              />
              <span class="toggle-switch"></span>
              <span class="toggle-text">Offline EAC</span>
            </label>
          </div>
          <button
            v-if="developerMode && game.can_uninstall !== false"
            class="danger-link danger-link-wide"
            title="Uninstall"
            @click="emit('uninstall')"
          >
            Uninstall
          </button>
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
          <div v-if="runtimeOpen" class="doctor-panel">
            <div v-if="runtimeLoading" class="doctor-loading">Checking bottle runtime...</div>
            <template v-else-if="runtimeReport">
              <div class="doctor-summary">
                <span class="badge" :class="runtimeReport.actions.length ? 'badge-warn' : 'badge-ok'">
                  {{ runtimeReport.actions.length ? "Bottle Repair" : "Bottle Ready" }}
                </span>
                <span>{{ runtimeReport.bottle_id ?? "steam prefix" }} / {{ runtimeReport.runtime_profile }}</span>
              </div>
              <div class="bottle-edit-row">
                <span>Bottle Name</span>
                <div class="bottle-input-row">
                  <input v-model="bottleName" class="bottle-name-input" type="text" :placeholder="game.name" />
                  <button class="icon-button compact-button" title="Use Steam name" @click="resetBottleName">
                    <svg
                      width="14"
                      height="14"
                      viewBox="0 0 24 24"
                      fill="none"
                      stroke="currentColor"
                      stroke-width="2"
                      stroke-linecap="round"
                      stroke-linejoin="round"
                    >
                      <path d="M3 12a9 9 0 1 0 9-9 9.75 9.75 0 0 0-6.74 2.74L3 8" />
                      <path d="M3 3v5h5" />
                    </svg>
                  </button>
                </div>
              </div>
              <div class="bottle-edit-row">
                <span>Graphics Backend</span>
                <select v-model="bottlePreferredMode" class="launch-mode-select" title="Bottle graphics backend">
                  <option v-for="option in bottlePipelineOptions" :key="option.id" :value="option.id">
                    {{ option.name }}
                  </option>
                </select>
              </div>
              <button class="btn btn-secondary btn-sm" :disabled="bottleSaving" @click="saveBottleEdit">
                {{ bottleSaving ? "Saving..." : "Save Bottle" }}
              </button>
              <div class="doctor-checks">
                <div v-for="component in runtimeReport.components" :key="component.id" class="doctor-check">
                  <span class="doctor-check-state">{{ component.state === "missing" ? "!" : "OK" }}</span>
                  <span class="doctor-check-label">{{ component.id }}</span>
                  <span class="doctor-check-detail">{{ component.state }}</span>
                </div>
              </div>
              <div v-if="runtimeReport.runtime_assets.length" class="doctor-notes">
                {{ runtimeReport.runtime_assets.length }} runtime assets detected near this install.
              </div>
              <div v-if="runtimeReport.actions.length" class="doctor-notes blocked">
                <div v-for="action in runtimeReport.actions" :key="action.id" class="runtime-action-row">
                  <span>{{ action.id }}: {{ action.detail }}</span>
                  <button
                    class="btn btn-secondary btn-sm"
                    :disabled="runtimeLoading"
                    @click="repairRuntimeComponent(action.id)"
                  >
                    Repair
                  </button>
                </div>
              </div>
            </template>
          </div>
        </div>
        <div v-else-if="steamInstalled" class="game-card-actions-stack">
          <button class="btn btn-install wide-action" @click="emit('install')">Install</button>
        </div>
        <span v-else class="badge badge-warn">Setup Steam</span>
      </div>
    </div>
  </div>
</template>

<style scoped>
.game-card {
  display: flex;
  flex-direction: column;
  background: var(--game-card-bg, var(--bg-card));
  border: 2px solid var(--border);
  border-radius: var(--radius-lg);
  overflow: hidden;
  transition:
    transform var(--transition),
    border-color var(--transition),
    box-shadow var(--transition);
  box-shadow:
    0 0 0 1px color-mix(in srgb, var(--accent) 14%, transparent),
    0 0 24px color-mix(in srgb, var(--accent) 16%, transparent),
    0 16px 36px color-mix(in srgb, var(--bg-deep) 34%, transparent);
}
.game-card.installed {
  box-shadow:
    0 0 0 1px color-mix(in srgb, var(--card-installed-glow-color) 35%, transparent),
    0 0 40px color-mix(in srgb, var(--card-installed-glow-color) 48%, transparent),
    0 18px 40px color-mix(in srgb, var(--bg-deep) 36%, transparent);
}
.game-card.uninstalled {
  box-shadow:
    0 0 0 1px color-mix(in srgb, var(--accent) 18%, transparent),
    0 0 24px color-mix(in srgb, var(--accent) 14%, transparent),
    0 14px 34px color-mix(in srgb, var(--bg-deep) 30%, transparent);
}
.game-card:hover {
  border-color: var(--border-strong);
  box-shadow:
    0 0 0 1px color-mix(in srgb, var(--accent) 32%, transparent),
    0 0 36px color-mix(in srgb, var(--accent) 26%, transparent),
    0 22px 48px color-mix(in srgb, var(--bg-deep) 42%, transparent);
  transform: translateY(-1px);
}
.game-card.running {
  border-color: var(--success);
  box-shadow:
    0 0 0 1px color-mix(in srgb, var(--success) 48%, transparent),
    0 0 38px color-mix(in srgb, var(--success) 30%, transparent),
    0 22px 48px color-mix(in srgb, var(--bg-deep) 42%, transparent);
}

.game-card-banner {
  width: 100%;
  aspect-ratio: 16 / 6.25;
  height: auto;
  background: var(--bg-surface);
  display: flex;
  align-items: center;
  justify-content: center;
  overflow: hidden;
  position: relative;
}
.game-card-cover {
  width: 100%;
  height: 100%;
  object-fit: cover;
  transition:
    transform 0.3s ease,
    filter var(--transition);
}
.game-card:hover .game-card-cover {
  transform: scale(1.015);
  filter: saturate(1.04);
}
.running-close-button {
  position: absolute;
  top: 8px;
  right: 8px;
  width: 30px;
  height: 30px;
  border: 1px solid color-mix(in srgb, var(--error) 44%, var(--border));
  border-radius: var(--radius-sm);
  background: color-mix(in srgb, var(--bg-deep) 82%, transparent);
  color: var(--error);
  display: inline-flex;
  align-items: center;
  justify-content: center;
  cursor: pointer;
  box-shadow: 0 8px 22px var(--card-glow);
}
.running-close-button:hover {
  border-color: var(--error);
  background: var(--error-bg);
}
.game-icon-placeholder {
  font-size: 36px;
  font-weight: 700;
  color: var(--text-dim);
  opacity: 0.4;
}

.game-card-body {
  display: flex;
  flex-direction: column;
  flex: 1 1 auto;
  background: var(--game-card-body-bg, transparent);
  color: var(--game-card-text, var(--text-primary));
  padding: 14px;
}
.game-card-heading {
  display: grid;
  grid-template-columns: minmax(0, 1fr) auto;
  gap: 10px;
  align-items: start;
  margin-bottom: 8px;
}
.game-card-title {
  font-size: 15px;
  font-weight: 700;
  line-height: 1.25;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}
.game-card-meta {
  display: flex;
  align-items: center;
  gap: 8px;
  min-height: 24px;
  margin-bottom: 12px;
  flex-wrap: wrap;
}
.game-card-size {
  font-size: 11px;
  color: var(--game-card-dim, var(--text-dim));
}
.route-chip {
  display: inline-flex;
  align-items: center;
  min-height: 22px;
  padding: 2px 8px;
  border-radius: var(--radius-sm);
  background: var(--success-bg);
  color: var(--success);
  font-size: 11px;
  font-weight: 700;
}
.bottle-chip {
  background: color-mix(in srgb, var(--accent) 14%, transparent);
  color: var(--accent);
}

.game-card-actions {
  margin-top: auto;
  min-height: 38px;
}
.game-card-actions-stack {
  display: flex;
  flex-direction: column;
  gap: 10px;
}
.primary-action-row {
  display: flex;
  align-items: center;
  gap: 10px;
  min-width: 0;
}
.primary-action-row .btn-play {
  flex: 1 1 auto;
}
.wide-action {
  width: 100%;
}
.secondary-action-grid {
  display: grid;
  grid-template-columns: minmax(0, 1fr) minmax(0, 1fr);
  gap: 8px;
  align-items: center;
  min-width: 0;
}
.tool-chip {
  min-height: 30px;
  padding: 4px 8px;
  border: 1px solid var(--border);
  border-radius: var(--radius-sm);
  background: color-mix(in srgb, var(--bg-input) 76%, transparent);
  overflow: hidden;
}
.toggle-text {
  min-width: 0;
  overflow: hidden;
  text-overflow: ellipsis;
}
.steam-emu-toggle {
  display: inline-flex;
  align-items: center;
  gap: 4px;
  font-size: 11px;
  padding: 2px 6px;
  min-height: 22px;
  vertical-align: middle;
}
.launch-mode-select {
  flex: 1 1 auto;
  min-width: 0;
  height: 34px;
  background: var(--bg-input);
  color: var(--text-primary);
  border: 1px solid var(--border-strong);
  border-radius: var(--radius-sm);
  padding: 4px 10px;
  font-size: 12px;
  outline: none;
}
.launch-mode-select:focus {
  border-color: var(--accent);
  box-shadow: 0 0 0 3px var(--accent-glow);
}
.icon-button {
  width: 34px;
  height: 34px;
  border: 1px solid var(--border-strong);
  border-radius: var(--radius-sm);
  background: var(--bg-input);
  color: var(--text-secondary);
  display: inline-flex;
  align-items: center;
  justify-content: center;
  cursor: pointer;
  transition: all var(--transition);
  flex: 0 0 34px;
}
.icon-button:hover {
  color: var(--accent);
  border-color: var(--accent-dim);
  background: var(--bg-card-hover);
}
.icon-button:disabled {
  opacity: 0.6;
  cursor: wait;
}
.icon-button .spinner {
  width: 14px;
  height: 14px;
}
.compact-button {
  width: 32px;
  height: 32px;
  flex-basis: 32px;
}
.bottle-button {
  color: var(--accent);
  border-color: color-mix(in srgb, var(--accent) 44%, var(--border));
  background: color-mix(in srgb, var(--accent) 12%, var(--bg-input));
}
.danger-link {
  min-height: 30px;
  padding: 4px 8px;
  border: 1px solid transparent;
  border-radius: var(--radius-sm);
  background: transparent;
  color: var(--error);
  font-size: 12px;
  font-weight: 700;
  cursor: pointer;
  transition: all var(--transition);
  white-space: nowrap;
}
.danger-link-wide {
  width: 100%;
  border-color: color-mix(in srgb, var(--error) 34%, var(--border));
  background: color-mix(in srgb, var(--error-bg) 76%, transparent);
}
.danger-link:hover {
  border-color: var(--error);
  background: var(--error-bg);
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
  background: color-mix(in srgb, var(--bg-surface) 84%, transparent);
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
.runtime-action-row {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 8px;
}
.runtime-action-row span {
  overflow-wrap: anywhere;
}
.bottle-edit-row {
  display: grid;
  grid-template-columns: 112px minmax(0, 1fr);
  gap: 8px;
  align-items: center;
  font-size: 11px;
  color: var(--text-secondary);
}
.bottle-input-row {
  display: flex;
  align-items: center;
  gap: 6px;
  min-width: 0;
}
.bottle-name-input {
  flex: 1 1 auto;
  min-width: 0;
  height: 32px;
  background: var(--bg-input);
  color: var(--text-primary);
  border: 1px solid var(--border-strong);
  border-radius: var(--radius-sm);
  padding: 4px 9px;
  font-size: 12px;
  outline: none;
}
.bottle-name-input:focus {
  border-color: var(--accent);
  box-shadow: 0 0 0 3px var(--accent-glow);
}

@media (max-width: 720px) {
  .secondary-action-grid {
    grid-template-columns: 1fr;
  }
  .bottle-edit-row {
    grid-template-columns: 1fr;
  }
  .doctor-check {
    grid-template-columns: 28px minmax(0, 1fr);
  }
  .doctor-check-detail {
    grid-column: 2;
  }
}
</style>
