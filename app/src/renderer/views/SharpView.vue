<script setup lang="ts">
import { ref, onMounted } from "vue";
import { useToast } from "../composables/useToast";
import { api, getAPI } from "../composables/useApi";
import type { SharpApp } from "../api-types";

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
    launch_args: string[];
  };
}

interface LogEntry {
  name: string;
  lines: string[];
}

interface CrashReport {
  file: string;
  name: string;
  source: string;
  timestamp: string;
  size_bytes: number;
}

interface BottleAction {
  id: string;
  status: string;
  detail: string;
}

interface BottleManifest {
  id: string;
  name: string;
  bottle_type: string;
  steam_app_id?: number | null;
  arch: string;
  runtime_profile: string;
  health: string;
  prefix_path: string;
  source_installer_path?: string | null;
  game_install_path?: string | null;
  runtime_assets: { id: string; kind: string; source_path: string; present: boolean }[];
  last_launch_log?: string | null;
  installed_components: { id: string; state: string }[];
  installed_app_detections: { name: string; exe_path: string; source: string }[];
}

interface BottleDiagnostic {
  id: string;
  ready: boolean;
  summary: string;
  actions: BottleAction[];
  checks: { id: string; ok: boolean; detail: string }[];
}

interface ComponentRepair {
  id: string;
  status: string;
  detail: string;
  asset_path?: string | null;
  log_path?: string | null;
  pid?: number | null;
}

const toast = useToast();
const apps = ref<SharpApp[]>([]);
const bottles = ref<BottleManifest[]>([]);
const bottleReports = ref<Record<string, BottleDiagnostic | null>>({});
const bottleLoading = ref<Record<string, boolean>>({});
const doctorOpen = ref<Record<string, boolean>>({});
const doctorLoading = ref<Record<string, boolean>>({});
const doctorReports = ref<Record<string, LaunchDoctorReport | null>>({});
const diagnosticsOpen = ref<Record<string, boolean>>({});
const diagnosticsLoading = ref<Record<string, boolean>>({});
const launchErrors = ref<Record<string, string>>({});
const recentLogLines = ref<Record<string, string[]>>({});
const recentCrashReports = ref<Record<string, CrashReport[]>>({});
const launchArgDrafts = ref<Record<string, string>>({});
const engineOptions = [
  { id: "auto", name: "Auto" },
  { id: "wine_bare", name: "Wine" },
  { id: "m11", name: "M11" },
  { id: "m12", name: "M12" },
  { id: "m10", name: "M10" },
  { id: "m9", name: "M9" },
  { id: "m32", name: "M32" },
];

async function load() {
  const [result, bottleResult] = await Promise.all([
    api<{ ok: boolean; apps: SharpApp[] }>("GET", "/sharp-library"),
    api<{ ok: boolean; bottles: BottleManifest[] }>("GET", "/bottles"),
  ]);
  if (result?.ok) {
    apps.value = result.apps;
    for (const app of result.apps) {
      if (launchArgDrafts.value[app.id] === undefined) {
        launchArgDrafts.value[app.id] = (app.user_launch_args ?? []).join(" ");
      }
    }
  }
  if (bottleResult?.ok) {
    bottles.value = bottleResult.bottles;
  }
}

async function refreshSharpLibrary() {
  await load();
  toast.show("Sharp Library refreshed", "success");
}

async function installExe() {
  const filePath = await getAPI().pickExeFile();
  if (!filePath) return;
  toast.show("Installing application...");
  const result = await api<{ ok: boolean; app?: SharpApp; installing?: boolean; message?: string; error?: string }>(
    "POST",
    "/sharp-library/install",
    { srcPath: filePath },
  );
  if (result?.ok && result.app) {
    toast.show(`Installed ${result.app.name}`, "success");
    await load();
  } else if (result?.ok && result.installing) {
    toast.show(result.message ?? "Installer started. Finish setup, then refresh Sharp Library.", "success");
    await load();
  } else {
    toast.show(result?.error ?? "Failed to install", "error");
  }
}

async function refreshBottle(id: string) {
  bottleLoading.value[id] = true;
  const result = await api<{ ok: boolean; bottle?: BottleManifest; error?: string }>("POST", "/bottles/refresh", { id });
  bottleLoading.value[id] = false;
  if (result?.ok && result.bottle) {
    upsertBottle(result.bottle);
    toast.show("Bottle scan refreshed", "success");
  } else {
    toast.show(result?.error ?? "Failed to refresh bottle", "error");
  }
}

async function doctorBottle(id: string) {
  bottleLoading.value[id] = true;
  const result = await api<{ ok: boolean; report?: BottleDiagnostic; error?: string }>("POST", "/bottles/doctor", { id });
  bottleLoading.value[id] = false;
  if (result?.ok && result.report) {
    bottleReports.value[id] = result.report;
    await load();
  } else {
    toast.show(result?.error ?? "Bottle Doctor failed", "error");
  }
}

async function prepareBottle(id: string) {
  bottleLoading.value[id] = true;
  const result = await api<{ ok: boolean; report?: BottleDiagnostic; error?: string }>("POST", "/bottles/prepare", { id });
  bottleLoading.value[id] = false;
  if (result?.ok && result.report) {
    bottleReports.value[id] = result.report;
    toast.show(result.report.ready ? "Bottle prepared" : "Bottle needs runtime repair", result.report.ready ? "success" : "error");
    await load();
  } else {
    toast.show(result?.error ?? "Failed to prepare bottle", "error");
  }
}

async function repairBottleComponent(id: string, component: string) {
  bottleLoading.value[id] = true;
  const result = await api<{ ok: boolean; repair?: ComponentRepair; error?: string }>("POST", "/bottles/repair-component", {
    id,
    component,
  });
  bottleLoading.value[id] = false;
  if (result?.ok && result.repair) {
    const repair = result.repair;
    const missing = repair.status === "asset_missing";
    toast.show(missing ? repair.detail : `${repair.id}: ${repair.status}`, missing ? "error" : "success");
    await doctorBottle(id);
  } else {
    toast.show(result?.error ?? "Failed to repair component", "error");
  }
}

async function addBottleApp(bottle: BottleManifest, app: { name: string; exe_path: string }) {
  bottleLoading.value[bottle.id] = true;
  const result = await api<{ ok: boolean; app?: SharpApp; error?: string }>("POST", "/sharp-library/import-bottle-app", {
    bottleId: bottle.id,
    exePath: app.exe_path,
    name: app.name,
  });
  bottleLoading.value[bottle.id] = false;
  if (result?.ok && result.app) {
    toast.show(`Added ${result.app.name}`, "success");
    await load();
  } else {
    toast.show(result?.error ?? "Failed to add bottle app", "error");
  }
}

function upsertBottle(bottle: BottleManifest) {
  const idx = bottles.value.findIndex((item) => item.id === bottle.id);
  if (idx >= 0) bottles.value[idx] = bottle;
  else bottles.value.push(bottle);
}

function bottleBadgeClass(health: string) {
  return health === "ready" ? "badge-ok" : "badge-warn";
}

async function launchApp(id: string, engine: string) {
  const app = apps.value.find((a) => a.id === id);
  if (!app) return;
  toast.show(`Launching ${app.name}...`);
  const result = await api<{ ok: boolean; pid?: number; pipeline?: string; warnings?: string[]; error?: string }>(
    "POST",
    "/sharp-library/launch",
    { id, engine },
  );
  if (result?.ok && result.pid) {
    const warning = result.warnings?.[0];
    launchErrors.value[id] = "";
    diagnosticsOpen.value[id] = false;
    toast.show(warning ? `Launched ${app.name}: ${warning}` : `Launched ${app.name}`, "success");
  } else {
    const error = result?.error ?? `Failed to launch ${app.name}`;
    launchErrors.value[id] = error;
    toast.show(error, "error");
    await openDiagnostics(app);
  }
}

async function updateEngine(id: string, engine: string) {
  const result = await api<{ ok: boolean; error?: string }>("POST", "/sharp-library/set-engine", { id, engine });
  if (result?.ok) {
    const app = apps.value.find((a) => a.id === id);
    if (app) app.engine = engine;
  } else {
    toast.show(result?.error ?? "Failed to set engine", "error");
  }
}

async function uninstallApp(id: string) {
  const app = apps.value.find((a) => a.id === id);
  if (!app) return;
  if (!confirm(`Uninstall ${app.name}?`)) return;
  const result = await api<{ ok: boolean; error?: string }>("POST", "/sharp-library/uninstall", { id });
  if (result?.ok) {
    toast.show(`Uninstalled ${app.name}`, "success");
    await load();
  } else toast.show(result?.error ?? "Failed to uninstall", "error");
}

async function setCover(id: string) {
  const filePath = await getAPI().pickImageFile();
  if (!filePath) return;
  const result = await api<{ ok: boolean; error?: string }>("POST", "/sharp-library/set-cover", {
    id,
    coverPath: filePath,
  });
  if (result?.ok) {
    toast.show("Cover updated", "success");
    await load();
  } else toast.show(result?.error ?? "Failed to set cover", "error");
}

async function updateCoverPosition(app: SharpApp) {
  const result = await api<{ ok: boolean; error?: string }>("POST", "/sharp-library/set-cover-position", {
    id: app.id,
    x: app.cover_position_x,
    y: app.cover_position_y,
  });
  if (!result?.ok) toast.show(result?.error ?? "Failed to save cover position", "error");
}

function coverPosition(app: SharpApp): string {
  return `${app.cover_position_x ?? 50}% ${app.cover_position_y ?? 50}%`;
}

function splitLaunchArgs(value: string): string[] {
  const matches = value.match(/(?:[^\s"]+|"[^"]*")+/g) ?? [];
  return matches.map((arg) => arg.replace(/^"|"$/g, "").trim()).filter(Boolean);
}

async function saveLaunchArgs(app: SharpApp) {
  const args = splitLaunchArgs(launchArgDrafts.value[app.id] ?? "");
  const result = await api<{ ok: boolean; error?: string }>("POST", "/sharp-library/set-launch-args", {
    id: app.id,
    args,
  });
  if (result?.ok) {
    app.user_launch_args = args;
    toast.show("Launch options saved", "success");
  } else {
    toast.show(result?.error ?? "Failed to save launch options", "error");
  }
}

async function runDoctor(app: SharpApp) {
  doctorOpen.value[app.id] = true;
  doctorLoading.value[app.id] = true;
  doctorReports.value[app.id] = null;
  const result = await api<{ ok: boolean; report?: LaunchDoctorReport; error?: string }>(
    "POST",
    "/sharp-library/doctor",
    {
      id: app.id,
      engine: app.engine,
    },
  );
  doctorLoading.value[app.id] = false;

  if (result?.ok && result.report) {
    doctorReports.value[app.id] = result.report;
  } else {
    toast.show(result?.error ?? "Launch Doctor failed", "error");
  }
}

async function openDiagnostics(app: SharpApp) {
  diagnosticsOpen.value[app.id] = true;
  await Promise.all([runDoctor(app), loadRecentDiagnostics(app)]);
}

async function loadRecentDiagnostics(app: SharpApp) {
  diagnosticsLoading.value[app.id] = true;
  const [logsResult, crashResult] = await Promise.all([
    api<{ ok: boolean; logs: LogEntry[] }>("GET", "/logs"),
    api<{ ok: boolean; reports: CrashReport[] }>("GET", "/logs/crash-reports"),
  ]);
  diagnosticsLoading.value[app.id] = false;

  if (logsResult?.ok) {
    const allLines = logsResult.logs.flatMap((entry) => entry.lines.map((line) => `[${entry.name}] ${line}`));
    const appNeedles = [app.name, app.exe_path, app.install_dir].map((value) => value.toLowerCase());
    const matching = allLines.filter((line) =>
      appNeedles.some((needle) => needle && line.toLowerCase().includes(needle)),
    );
    recentLogLines.value[app.id] = (matching.length ? matching : allLines).slice(-40);
  }

  if (crashResult?.ok) {
    const appNeedles = [app.name, app.exe_path, app.install_dir].map((value) => value.toLowerCase());
    recentCrashReports.value[app.id] = crashResult.reports
      .filter((report) => {
        const haystack = `${report.name} ${report.file} ${report.source}`.toLowerCase();
        return appNeedles.some((needle) => needle && haystack.includes(needle));
      })
      .slice(0, 5);
  }
}

function doctorActionLabel(check: LaunchDoctorCheck, app: SharpApp): string {
  if (check.id === "runtime_assets" || check.id === "dll_sources") return "Install runtime";
  if (check.id === "exe_route") return app.engine === "auto" ? "Switch to Wine" : "Switch to Auto";
  if (check.detail.toLowerCase().includes("steam")) return "Restart Steam";
  if (check.id === "launcher_exe") return "Open logs";
  return "Open logs";
}

async function runDoctorAction(app: SharpApp, check: LaunchDoctorCheck) {
  const label = doctorActionLabel(check, app);
  if (label === "Install runtime") {
    const result = await api<{ ok: boolean; error?: string }>("POST", "/setup/install-all");
    toast.show(
      result?.ok ? "Runtime install started" : (result?.error ?? "Failed to start runtime install"),
      result?.ok ? "success" : "error",
    );
  } else if (label === "Restart Steam") {
    await api("POST", "/steam/stop");
    const result = await api<{ ok: boolean; error?: string }>("POST", "/steam/launch");
    toast.show(
      result?.ok ? "Steam restart requested" : (result?.error ?? "Failed to restart Steam"),
      result?.ok ? "success" : "error",
    );
  } else if (label === "Switch to Auto") {
    await updateEngine(app.id, "auto");
    await runDoctor({ ...app, engine: "auto" });
  } else if (label === "Switch to Wine") {
    await updateEngine(app.id, "wine_bare");
    await runDoctor({ ...app, engine: "wine_bare" });
  } else {
    await openLogFolder();
  }
}

async function clearShaderCache(app: SharpApp) {
  const result = await api<{ ok: boolean; bytes_freed?: number; files_removed?: number; error?: string }>(
    "POST",
    "/cache/clear",
    { type: "shader" },
  );
  if (result?.ok) {
    toast.show(`All shader caches cleared before next ${app.name} launch`, "success");
  } else {
    toast.show(result?.error ?? "Failed to clear shader cache", "error");
  }
}

async function openLogFolder() {
  const result = await getAPI().openLogsFolder();
  if (result && result.ok === false) {
    toast.show(result.error ?? "Failed to open logs", "error");
  }
}

async function copyDiagnosticBundle(app: SharpApp) {
  const report = doctorReports.value[app.id];
  const payload = [
    `MetalSharp Sharp Library Diagnostic Bundle`,
    `App: ${app.name}`,
    `ID: ${app.id}`,
    `Engine: ${app.engine}`,
    `EXE: ${app.install_dir}/${app.exe_path}`,
    `Last launch error: ${launchErrors.value[app.id] || "none"}`,
    "",
    "Doctor:",
    report ? JSON.stringify(report, null, 2) : "No doctor report loaded",
    "",
    "Recent crash reports:",
    JSON.stringify(recentCrashReports.value[app.id] ?? [], null, 2),
    "",
    "Recent launch log:",
    (recentLogLines.value[app.id] ?? []).join("\n"),
  ].join("\n");
  const result = await getAPI().copyText(payload);
  toast.show(
    result?.ok ? "Diagnostic bundle copied" : (result?.error ?? "Failed to copy diagnostics"),
    result?.ok ? "success" : "error",
  );
}

function formatBytes(bytes: number): string {
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
  if (bytes < 1024 * 1024 * 1024) return `${(bytes / (1024 * 1024)).toFixed(1)} MB`;
  return `${(bytes / (1024 * 1024 * 1024)).toFixed(1)} GB`;
}

onMounted(load);
</script>

<template>
  <div class="sharp-view">
    <div class="sharp-header">
      <div>
        <h1>Sharp Library</h1>
        <p class="subtitle">Windows applications running via MetalSharp Wine</p>
      </div>
      <div class="sharp-header-actions">
        <button class="btn btn-secondary" @click="refreshSharpLibrary">Refresh</button>
        <button class="btn btn-primary" @click="installExe">Install Windows Program</button>
      </div>
    </div>

    <section v-if="bottles.length" class="bottle-strip">
      <div class="bottle-strip-header">
        <div>
          <h2>Runtime Bottles</h2>
          <p>{{ bottles.length }} runtime {{ bottles.length === 1 ? "prefix" : "prefixes" }} tracked</p>
        </div>
      </div>
      <div class="bottle-list">
        <article v-for="bottle in bottles" :key="bottle.id" class="bottle-card">
          <div class="bottle-card-main">
            <div>
              <div class="bottle-title">{{ bottle.name }}</div>
              <div class="bottle-meta">
                <span class="badge" :class="bottleBadgeClass(bottle.health)">{{ bottle.health }}</span>
                <span>{{ bottle.bottle_type }}</span>
                <span>{{ bottle.arch }}</span>
                <span>{{ bottle.runtime_profile }}</span>
                <span v-if="bottle.steam_app_id">appid {{ bottle.steam_app_id }}</span>
              </div>
            </div>
            <div class="bottle-actions">
              <button class="btn btn-secondary btn-sm" :disabled="bottleLoading[bottle.id]" @click="doctorBottle(bottle.id)">
                {{ bottleLoading[bottle.id] ? "Checking" : "Doctor" }}
              </button>
              <button class="btn btn-secondary btn-sm" :disabled="bottleLoading[bottle.id]" @click="prepareBottle(bottle.id)">
                Prepare
              </button>
              <button class="btn btn-secondary btn-sm" :disabled="bottleLoading[bottle.id]" @click="refreshBottle(bottle.id)">
                Scan
              </button>
            </div>
          </div>
          <div class="bottle-components">
            <span v-for="component in bottle.installed_components" :key="component.id" class="component-pill">
              {{ component.id }}: {{ component.state }}
            </span>
            <span v-if="bottle.runtime_assets?.length" class="component-pill">
              assets: {{ bottle.runtime_assets.length }}
            </span>
          </div>
          <div v-if="bottle.installed_app_detections?.length" class="bottle-detections">
            <button
              v-for="candidate in bottle.installed_app_detections.slice(0, 3)"
              :key="candidate.exe_path"
              class="btn btn-secondary btn-sm"
              :disabled="bottleLoading[bottle.id]"
              @click="addBottleApp(bottle, candidate)"
            >
              Add {{ candidate.name }}
            </button>
          </div>
          <div v-if="bottleReports[bottle.id]" class="bottle-report">
            <div class="doctor-summary">
              <span class="badge" :class="bottleReports[bottle.id]?.ready ? 'badge-ok' : 'badge-warn'">
                {{ bottleReports[bottle.id]?.ready ? "Ready" : "Repair" }}
              </span>
              <span>{{ bottleReports[bottle.id]?.summary }}</span>
            </div>
            <div v-if="bottleReports[bottle.id]?.actions.length" class="doctor-notes blocked">
              <div v-for="action in bottleReports[bottle.id]?.actions" :key="action.id" class="bottle-action-row">
                <span>{{ action.id }}: {{ action.detail }}</span>
                <button
                  class="btn btn-secondary btn-sm"
                  :disabled="bottleLoading[bottle.id]"
                  @click="repairBottleComponent(bottle.id, action.id)"
                >
                  Repair
                </button>
              </div>
            </div>
          </div>
        </article>
      </div>
    </section>

    <div v-if="apps.length === 0" class="empty-state">
      <div class="empty-icon">
        <svg width="48" height="48" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5">
          <rect x="2" y="3" width="20" height="14" rx="2" />
          <line x1="8" y1="21" x2="16" y2="21" />
          <line x1="12" y1="17" x2="12" y2="21" />
        </svg>
      </div>
      <h2>No applications installed</h2>
      <p>Click "Install an EXE" to add a Windows application</p>
    </div>

    <div v-else class="sharp-grid">
      <div v-for="app in apps" :key="app.id" class="sharp-card">
        <div class="sharp-card-banner">
          <img
            v-if="app.cover"
            :src="`http://127.0.0.1:9274/sharp-library/cover?id=${app.id}`"
            :alt="app.name"
            :style="{ objectPosition: coverPosition(app) }"
          />
          <span v-else class="sharp-icon-placeholder">{{ app.name.charAt(0) }}</span>
        </div>
        <div class="sharp-card-body">
          <div class="sharp-card-title">{{ app.name }}</div>
          <div class="sharp-card-meta">
            <span class="badge badge-ok">Sharp App</span>
            <span class="sharp-card-size">{{ formatBytes(app.size_bytes) }}</span>
          </div>
          <div class="sharp-card-actions">
            <div class="sharp-card-actions-row">
              <button class="btn btn-play" @click="launchApp(app.id, app.engine)">Play</button>
              <select
                class="control-input"
                :value="app.engine"
                @change="updateEngine(app.id, ($event.target as HTMLSelectElement).value)"
              >
                <option v-for="option in engineOptions" :key="option.id" :value="option.id">
                  {{ option.name }}
                </option>
              </select>
            </div>
            <div class="sharp-card-actions-row subtle">
              <button class="btn btn-secondary btn-sm" @click="setCover(app.id)">Set Cover</button>
              <button class="btn btn-secondary btn-sm" :disabled="doctorLoading[app.id]" @click="runDoctor(app)">
                {{ doctorLoading[app.id] ? "Checking" : "Doctor" }}
              </button>
              <button
                class="btn btn-secondary btn-sm"
                :disabled="diagnosticsLoading[app.id]"
                @click="openDiagnostics(app)"
              >
                {{ diagnosticsLoading[app.id] ? "Loading" : "Diagnostics" }}
              </button>
            </div>
            <div v-if="app.cover" class="cover-position-controls">
              <label>
                <span>X</span>
                <input
                  v-model.number="app.cover_position_x"
                  type="range"
                  min="0"
                  max="100"
                  @change="updateCoverPosition(app)"
                />
              </label>
              <label>
                <span>Y</span>
                <input
                  v-model.number="app.cover_position_y"
                  type="range"
                  min="0"
                  max="100"
                  @change="updateCoverPosition(app)"
                />
              </label>
            </div>
            <div class="launch-options-row">
              <input
                v-model="launchArgDrafts[app.id]"
                class="control-input launch-options-input"
                type="text"
                placeholder="Launch options..."
                @keydown.enter="saveLaunchArgs(app)"
              />
              <button class="btn btn-secondary btn-sm" @click="saveLaunchArgs(app)">Save</button>
            </div>
            <div class="sharp-card-danger-row">
              <button class="btn btn-danger btn-sm sharp-uninstall-button" @click="uninstallApp(app.id)">
                Uninstall
              </button>
            </div>
            <div v-if="launchErrors[app.id]" class="launch-failure">
              <span>Last launch failed</span>
              <strong>{{ launchErrors[app.id] }}</strong>
            </div>
            <details v-if="doctorOpen[app.id]" class="doctor-panel" open>
              <summary class="drawer-summary">
                <span>Launch Doctor</span>
                <small>{{ doctorReports[app.id]?.summary ?? "Checking launch prerequisites" }}</small>
              </summary>
              <div v-if="doctorLoading[app.id]" class="doctor-loading">Checking launch prerequisites...</div>
              <template v-else-if="doctorReports[app.id]">
                <div class="doctor-summary">
                  <span class="badge" :class="doctorReports[app.id]?.ready ? 'badge-ok' : 'badge-warn'">
                    {{ doctorReports[app.id]?.ready ? "Ready" : "Blocked" }}
                  </span>
                  <span>{{ doctorReports[app.id]?.summary }}</span>
                </div>
                <div class="doctor-checks">
                  <div
                    v-for="check in doctorReports[app.id]?.checks ?? []"
                    :key="check.id"
                    class="doctor-check"
                    :class="{ failed: !check.ok }"
                  >
                    <span class="doctor-check-state">{{ check.ok ? "OK" : "!" }}</span>
                    <span class="doctor-check-label">{{ check.label }}</span>
                    <span class="doctor-check-detail">
                      {{ check.detail }}
                      <button
                        v-if="!check.ok || check.id === 'launcher_exe'"
                        class="doctor-action"
                        @click="runDoctorAction(app, check)"
                      >
                        {{ doctorActionLabel(check, app) }}
                      </button>
                    </span>
                  </div>
                </div>
                <div v-if="doctorReports[app.id]?.recipe.launch_args.length" class="doctor-notes">
                  <div>Args: {{ doctorReports[app.id]?.recipe.launch_args.join(" ") }}</div>
                </div>
                <div v-if="doctorReports[app.id]?.blockers.length" class="doctor-notes blocked">
                  <div v-for="blocker in doctorReports[app.id]?.blockers" :key="blocker">{{ blocker }}</div>
                </div>
                <div v-if="doctorReports[app.id]?.warnings.length" class="doctor-notes">
                  <div v-for="warning in doctorReports[app.id]?.warnings" :key="warning">{{ warning }}</div>
                </div>
              </template>
            </details>
            <details v-if="diagnosticsOpen[app.id]" class="diagnostics-panel" open>
              <summary class="drawer-summary">
                <span>Logs and crash reports</span>
                <small
                  >{{ recentCrashReports[app.id]?.length ?? 0 }} crash reports ·
                  {{ recentLogLines[app.id]?.length ?? 0 }} log lines</small
                >
              </summary>
              <div class="diagnostics-toolbar">
                <button class="btn btn-secondary btn-sm" @click="clearShaderCache(app)">Clear All Shader Caches</button>
                <button class="btn btn-secondary btn-sm" @click="openLogFolder">Open Logs</button>
                <button class="btn btn-secondary btn-sm" @click="copyDiagnosticBundle(app)">Copy Bundle</button>
              </div>
              <div v-if="recentCrashReports[app.id]?.length" class="diagnostics-section">
                <div class="diagnostics-title">Recent crash reports</div>
                <div v-for="report in recentCrashReports[app.id]" :key="report.file" class="crash-row">
                  <span>{{ report.name }}</span>
                  <small>{{ report.timestamp }} · {{ report.source }}</small>
                </div>
              </div>
              <div class="diagnostics-section">
                <div class="diagnostics-title">Recent launch log</div>
                <pre class="log-tail">{{ (recentLogLines[app.id] ?? ["No recent log lines loaded."]).join("\n") }}</pre>
              </div>
            </details>
          </div>
        </div>
      </div>
    </div>
  </div>
</template>

<style scoped>
.sharp-view {
  padding: 24px 28px;
  height: 100%;
  overflow-y: auto;
}
.sharp-header {
  display: flex;
  justify-content: space-between;
  align-items: flex-start;
  gap: 16px;
  margin: -24px -28px 20px;
  padding: 24px 28px 18px;
  background: var(--page-header-bg);
  border-bottom: 1px solid var(--border);
}
.sharp-header-actions {
  display: flex;
  align-items: center;
  gap: 8px;
  flex-wrap: wrap;
  justify-content: flex-end;
}
.sharp-header h1 {
  font-size: 22px;
  font-weight: 600;
}
.subtitle {
  font-size: 12px;
  color: var(--text-dim);
  margin-top: 2px;
}

.bottle-strip {
  margin-bottom: 18px;
}
.bottle-strip-header {
  display: flex;
  align-items: end;
  justify-content: space-between;
  margin-bottom: 8px;
}
.bottle-strip-header h2 {
  font-size: 14px;
  font-weight: 700;
}
.bottle-strip-header p {
  margin-top: 2px;
  color: var(--text-dim);
  font-size: 11px;
}
.bottle-list {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(300px, 1fr));
  gap: 10px;
}
.bottle-card {
  padding: 12px;
  border: 1px solid var(--border);
  border-radius: var(--radius-md);
  background: var(--bg-card);
}
.bottle-card-main {
  display: flex;
  align-items: flex-start;
  justify-content: space-between;
  gap: 12px;
}
.bottle-title {
  max-width: 220px;
  overflow: hidden;
  color: var(--text-primary);
  font-size: 13px;
  font-weight: 700;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.bottle-meta,
.bottle-components {
  display: flex;
  flex-wrap: wrap;
  gap: 6px;
  margin-top: 7px;
  color: var(--text-dim);
  font-size: 10px;
}
.bottle-actions {
  display: flex;
  flex-wrap: wrap;
  justify-content: flex-end;
  gap: 6px;
}
.bottle-detections {
  display: flex;
  flex-wrap: wrap;
  gap: 6px;
  margin-top: 8px;
}
.component-pill {
  padding: 3px 6px;
  border: 1px solid var(--border);
  border-radius: var(--radius-sm);
  color: var(--text-secondary);
}
.bottle-report {
  margin-top: 10px;
  padding-top: 10px;
  border-top: 1px solid var(--border);
}
.bottle-action-row {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 8px;
  margin-top: 6px;
}
.bottle-action-row span {
  overflow-wrap: anywhere;
}

.sharp-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(280px, 1fr));
  gap: 16px;
}

.sharp-card {
  background: var(--bg-card);
  border: 1px solid var(--border);
  border-radius: var(--radius-lg);
  overflow: hidden;
}
.sharp-card-banner {
  width: 100%;
  height: 140px;
  background: var(--bg-surface);
  display: flex;
  align-items: center;
  justify-content: center;
  overflow: hidden;
}
.sharp-card-banner img {
  width: 100%;
  height: 100%;
  object-fit: cover;
}
.sharp-icon-placeholder {
  font-size: 36px;
  font-weight: 700;
  color: var(--text-dim);
  opacity: 0.4;
}
.sharp-card-body {
  padding: 12px 14px 14px;
}
.sharp-card-title {
  font-size: 14px;
  font-weight: 600;
  margin-bottom: 6px;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}
.sharp-card-meta {
  display: flex;
  align-items: center;
  gap: 6px;
  margin-bottom: 10px;
}
.sharp-card-size {
  font-size: 11px;
  color: var(--text-dim);
}
.sharp-card-actions {
  display: flex;
  flex-direction: column;
  gap: 8px;
}
.sharp-card-actions-row {
  display: flex;
  align-items: center;
  gap: 8px;
}
.sharp-card-actions-row.subtle {
  opacity: 0.7;
  flex-wrap: wrap;
}
.cover-position-controls {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 8px;
  padding: 8px;
  border: 1px solid var(--border);
  border-radius: var(--radius-sm);
  background: color-mix(in srgb, var(--bg-surface) 76%, transparent);
}
.cover-position-controls label {
  display: grid;
  grid-template-columns: 14px minmax(0, 1fr);
  align-items: center;
  gap: 6px;
  color: var(--text-dim);
  font-size: 10px;
  font-weight: 700;
}
.cover-position-controls input {
  min-width: 0;
}
.launch-options-row {
  display: grid;
  grid-template-columns: minmax(0, 1fr) auto;
  gap: 8px;
}
.launch-options-input {
  width: 100%;
}
.sharp-card-danger-row {
  display: flex;
}
.sharp-uninstall-button {
  width: 100%;
}

.launch-failure {
  display: flex;
  flex-direction: column;
  gap: 3px;
  padding: 8px 10px;
  border: 1px solid color-mix(in srgb, var(--danger) 50%, var(--border));
  border-radius: var(--radius-md);
  background: color-mix(in srgb, var(--danger) 12%, var(--bg-surface));
  color: var(--text-secondary);
  font-size: 11px;
  line-height: 1.35;
}
.launch-failure span {
  color: var(--danger);
  font-weight: 700;
}
.launch-failure strong {
  font-weight: 500;
  overflow-wrap: anywhere;
}

.doctor-panel {
  margin-top: 2px;
  padding: 10px;
  background: var(--bg-surface);
  border: 1px solid var(--border);
  border-radius: var(--radius-md);
  font-size: 11px;
}
.drawer-summary {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 10px;
  cursor: pointer;
  color: var(--text-secondary);
  font-size: 11px;
  font-weight: 700;
  list-style: none;
}
.drawer-summary::-webkit-details-marker {
  display: none;
}
.drawer-summary::after {
  content: "v";
  color: var(--text-dim);
  transition: transform 120ms ease;
}
details:not([open]) > .drawer-summary::after {
  transform: rotate(-90deg);
}
.drawer-summary small {
  min-width: 0;
  flex: 1;
  color: var(--text-dim);
  font-size: 10px;
  font-weight: 500;
  overflow: hidden;
  text-align: right;
  text-overflow: ellipsis;
  white-space: nowrap;
}
details[open] > .drawer-summary {
  margin-bottom: 10px;
}
.doctor-loading {
  color: var(--text-dim);
}
.doctor-summary {
  display: flex;
  align-items: center;
  gap: 8px;
  margin-bottom: 8px;
  color: var(--text-secondary);
}
.doctor-checks {
  display: flex;
  flex-direction: column;
  gap: 5px;
}
.doctor-check {
  display: grid;
  grid-template-columns: 28px minmax(68px, 82px) 1fr;
  gap: 6px;
  align-items: start;
  color: var(--text-dim);
}
.doctor-check.failed {
  color: var(--text-primary);
}
.doctor-check-state {
  font-size: 9px;
  color: var(--text-dim);
}
.doctor-check-label {
  color: var(--text-secondary);
}
.doctor-check-detail {
  overflow-wrap: anywhere;
}
.doctor-action {
  display: inline-flex;
  align-items: center;
  min-height: 22px;
  margin-top: 5px;
  padding: 3px 8px;
  border: 1px solid var(--border-strong);
  border-radius: var(--radius-sm);
  background: var(--bg-elevated);
  color: var(--text-secondary);
  font-size: 10px;
  font-weight: 600;
  cursor: pointer;
}
.doctor-action:hover {
  border-color: var(--accent);
  color: var(--text-primary);
}
.doctor-notes {
  margin-top: 8px;
  color: var(--text-dim);
  line-height: 1.4;
}
.doctor-notes.blocked {
  color: var(--danger);
}

.diagnostics-panel {
  padding: 10px;
  border: 1px solid var(--border);
  border-radius: var(--radius-md);
  background: var(--bg-surface);
}
.diagnostics-toolbar {
  display: flex;
  flex-wrap: wrap;
  gap: 6px;
  margin-bottom: 10px;
}
.diagnostics-section + .diagnostics-section {
  margin-top: 10px;
}
.diagnostics-title {
  margin-bottom: 5px;
  color: var(--text-secondary);
  font-size: 11px;
  font-weight: 700;
}
.crash-row {
  display: flex;
  flex-direction: column;
  gap: 2px;
  padding: 6px 0;
  border-top: 1px solid var(--border);
  font-size: 11px;
  color: var(--text-secondary);
}
.crash-row small {
  color: var(--text-dim);
}
.log-tail {
  max-height: 180px;
  overflow: auto;
  margin: 0;
  padding: 10px;
  border: 1px solid var(--border);
  border-radius: var(--radius-sm);
  background: var(--bg-deep);
  color: var(--text-secondary);
  font-family: var(--font-mono);
  font-size: 10px;
  line-height: 1.55;
  white-space: pre-wrap;
  overflow-wrap: anywhere;
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
}
</style>
