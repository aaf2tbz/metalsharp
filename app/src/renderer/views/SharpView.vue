<script setup lang="ts">
import { computed, ref, onMounted, onUnmounted } from "vue";
import { useToast } from "../composables/useToast";
import { api, getAPI } from "../composables/useApi";
import type { SharpApp } from "../api-types";
import IconGlassWater from "~icons/lucide/glass-water";
import IconLayoutGrid from "~icons/lucide/layout-grid";
import IconDownload from "~icons/lucide/download";
import IconUpload from "~icons/lucide/upload";
import IconRefreshCcw from "~icons/lucide/refresh-ccw";
import IconMonitor from "~icons/lucide/monitor";
import IconX from "~icons/lucide/x";

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

interface RuntimeProfileDefinition {
  id: string;
  name: string;
  components: string[];
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
  last_launch_pid?: number | null;
  last_launch_status?: string | null;
  last_launch_finished_at?: string | null;
  installed_components: { id: string; state: string }[];
  installed_app_detections: { name: string; exe_path: string; source: string }[];
}

interface BottleDiagnostic {
  id: string;
  ready: boolean;
  summary: string;
  actions: BottleAction[];
  checks: { id: string; ok: boolean; detail: string }[];
  component_sources?: { id: string; source: string; available: boolean; detail: string; path?: string | null }[];
}

interface ComponentRepair {
  id: string;
  status: string;
  detail: string;
  asset_path?: string | null;
  log_path?: string | null;
  pid?: number | null;
}

interface CompatibilityCase {
  id: string;
  name: string;
  case_type: string;
  required_profile: string;
  installer_opens: string;
  final_app_detected: string;
  final_app_launches: string;
  known_missing_runtime: string;
  bottle_id?: string | null;
  notes?: string;
  evidence_updated_at?: string | null;
  per_game_prefix_recommendation?: string;
}

interface RedistSourceGuide {
  id: string;
  name: string;
  source_url: string;
  local_targets: string[];
  policy: string;
  notes: string;
}

const toast = useToast();
const apps = ref<SharpApp[]>([]);
const dropdownOpen = ref<string | null>(null);
const dropdownStyle = ref<Record<string, string>>({});

function openDropdown(name: string, event: MouseEvent) {
  if (dropdownOpen.value === name) {
    dropdownOpen.value = null;
    return;
  }
  const btn = (event.currentTarget as HTMLElement).getBoundingClientRect();
  dropdownStyle.value = {
    top: `${btn.bottom + 6}px`,
    left: `${btn.left}px`,
    width: name === 'compat' ? '680px' : '420px',
  };
  dropdownOpen.value = name;
}
const bottles = ref<BottleManifest[]>([]);
const runtimeProfiles = ref<RuntimeProfileDefinition[]>([]);
const compatibilityCases = ref<CompatibilityCase[]>([]);
const redistSources = ref<RedistSourceGuide[]>([]);
const bottleReports = ref<Record<string, BottleDiagnostic | null>>({});
const bottleLoading = ref<Record<string, boolean>>({});
const bottleAdvancedOpen = ref<Record<string, boolean>>({});
const doctorOpen = ref<Record<string, boolean>>({});
const doctorLoading = ref<Record<string, boolean>>({});
const doctorReports = ref<Record<string, LaunchDoctorReport | null>>({});
const diagnosticsOpen = ref<Record<string, boolean>>({});
const diagnosticsLoading = ref<Record<string, boolean>>({});
const launchErrors = ref<Record<string, string>>({});
const runningSharpPids = ref<Record<string, number>>({});
const recentLogLines = ref<Record<string, string[]>>({});
const recentCrashReports = ref<Record<string, CrashReport[]>>({});
const launchArgDrafts = ref<Record<string, string>>({});
const engineOptions = [
  { id: "d3dmetal", name: "D3DMetal" },
  { id: "m12", name: "M12" },
  { id: "m11", name: "M11" },
  { id: "m10", name: "M10" },
  { id: "m9", name: "M9" },
  { id: "fna_arm64", name: "Mono/FNA" },
];

const componentDisplayName: Record<string, string> = {
  "mono-arm64": "Mono ARM64",
  "mono-x86": "Mono x86_64",
  "fna": "FNA Runtime",
  "xna": "XNA Assemblies",
  "sdl2": "SDL2",
  "fna3d": "FNA3D",
  "faudio": "FAudio",
  "fmod": "FMOD Audio",
  "d3d12_agility": "D3D12 Agility",
  "gpu_vendor_stubs": "GPU Stubs",
  "gptk_amd_stub": "GPTK AMD Stub",
  "gptk": "GPTK",
  "gptk_prefix": "GPTK Prefix",
  "rosetta": "Rosetta",
  "corefonts": "Core Fonts",
  "vcrun2019_x64": "VC++ 2015-2022 x64",
  "vcrun2019_x86": "VC++ 2015-2022 x86",
  "vcrun2019": "VC++ 2015-2022",
  "vcrun2010": "VC++ 2010",
  "vcrun2013": "VC++ 2013",
  "dotnet40": ".NET 4.0",
  "dotnet48": ".NET 4.8",
  "webview2": "WebView2",
  "directx_jun2010": "DX Jun2010",
  "openal": "OpenAL",
  "physx": "PhysX",
  "easyanticheat_eos": "EAC EOS",
  "battleye": "BattlEye",
};

const fnaComponentIds = new Set(["mono-arm64", "mono-x86", "fna", "xna", "sdl2", "fna3d", "faudio", "fmod"]);

function componentLabel(id: string): string {
  return componentDisplayName[id] ?? id;
}

function componentStateClass(state: string): string {
  if (state === "installed" || state === "ready") return "pill-ok";
  if (state === "missing") return "pill-missing";
  if (state === "needs_repair" || state === "partial") return "pill-warn";
  return "pill-unknown";
}

function isFnaProfile(profile: string): boolean {
  return profile === "fna_arm64" || profile === "fna_x86";
}
const selectableRuntimeProfileIds = new Set(["m12", "d3dmetal", "m11", "m10", "m9", "fna_arm64"]);
const visibleRuntimeProfiles = computed(() =>
  runtimeProfiles.value
    .filter((profile) => selectableRuntimeProfileIds.has(profile.id))
    .map((profile) => ({
      ...profile,
      name: profile.id === "fna_arm64" ? "Mono/FNA" : profile.name.replace(/^D3D(\d+) Metal$/, "M$1"),
    })),
);

function sharpAppNameSort(a: SharpApp, b: SharpApp) {
  return a.name.localeCompare(b.name, undefined, { sensitivity: "base", numeric: true });
}

async function load() {
  const [result, bottleResult, profileResult, matrixResult, redistResult] = await Promise.all([
    api<{ ok: boolean; apps: SharpApp[] }>("GET", "/sharp-library"),
    api<{ ok: boolean; bottles: BottleManifest[] }>("GET", "/bottles"),
    api<{ ok: boolean; profiles: RuntimeProfileDefinition[] }>("GET", "/bottles/profiles"),
    api<{ ok: boolean; cases: CompatibilityCase[] }>("GET", "/bottles/compatibility-matrix"),
    api<{ ok: boolean; sources: RedistSourceGuide[] }>("GET", "/bottles/redist-sources"),
  ]);
  if (result?.ok) {
    apps.value = [...result.apps].sort(sharpAppNameSort);
    for (const app of apps.value) {
      if (launchArgDrafts.value[app.id] === undefined) {
        launchArgDrafts.value[app.id] = (app.user_launch_args ?? []).join(" ");
      }
    }
  }
  if (bottleResult?.ok) {
    bottles.value = bottleResult.bottles;
  }
  if (profileResult?.ok) runtimeProfiles.value = profileResult.profiles;
  if (matrixResult?.ok) compatibilityCases.value = matrixResult.cases;
  if (redistResult?.ok) redistSources.value = redistResult.sources;
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
  if (result?.ok && result.repair) {
    const repair = result.repair;
    const failed = ["asset_missing", "failed", "install_failed"].includes(repair.status);
    toast.show(failed ? repair.detail : `${repair.id}: ${repair.status}`, failed ? "error" : "success");
    if (repair.status === "started" || repair.status === "seeding") {
      await pollRepairDone(id, component);
    } else {
      await doctorBottle(id);
    }
  } else {
    toast.show(result?.error ?? "Failed to repair component", "error");
  }
  bottleLoading.value[id] = false;
}

async function pollRepairDone(id: string, component: string) {
  const pollInterval = 5000;
  const maxPolls = 120;
  for (let i = 0; i < maxPolls; i++) {
    await new Promise((r) => setTimeout(r, pollInterval));
    const poll = await api<{ ok: boolean; repair?: ComponentRepair; error?: string }>("POST", "/bottles/repair-component", {
      id,
      component,
      dryRun: true,
    });
    if (!poll?.ok || !poll.repair) break;
    const status = poll.repair.status;
    if (status === "already_installed") {
      toast.show(`${component}: ready`, "success");
      await doctorBottle(id);
      return;
    }
    if (["asset_missing", "failed", "install_failed"].includes(status)) {
      toast.show(poll.repair.detail || `${component}: ${status}`, "error");
      await doctorBottle(id);
      return;
    }
  }
  toast.show(`${component}: repair is taking longer than expected — check back`);
  await doctorBottle(id);
}

async function setBottleProfile(id: string, profile: string) {
  bottleLoading.value[id] = true;
  const result = await api<{ ok: boolean; bottle?: BottleManifest; error?: string }>("POST", "/bottles/set-runtime-profile", {
    id,
    profile,
  });
  bottleLoading.value[id] = false;
  if (result?.ok && result.bottle) {
    upsertBottle(result.bottle);
    toast.show("Bottle profile updated", "success");
    await doctorBottle(id);
  } else {
    toast.show(result?.error ?? "Failed to update bottle profile", "error");
  }
}

async function setBottleWindowsVersion(id: string, version: string) {
  bottleLoading.value[id] = true;
  const result = await api<{ ok: boolean; repair?: ComponentRepair; error?: string }>("POST", "/bottles/set-windows-version", {
    id,
    version,
  });
  bottleLoading.value[id] = false;
  if (result?.ok && result.repair) {
    toast.show(`Windows mode ${version} requested`, "success");
    await doctorBottle(id);
  } else {
    toast.show(result?.error ?? "Failed to set Windows mode", "error");
  }
}

async function relaunchBottleInstaller(bottle: BottleManifest) {
  bottleLoading.value[bottle.id] = true;
  const result = await api<{ ok: boolean; installing?: boolean; message?: string; error?: string }>(
    "POST",
    "/bottles/relaunch-installer",
    { id: bottle.id },
  );
  bottleLoading.value[bottle.id] = false;
  if (result?.ok) {
    toast.show(result.message ?? "Installer relaunched", "success");
    await load();
  } else {
    toast.show(result?.error ?? "Failed to relaunch installer", "error");
  }
}

async function recordCompatibility(item: CompatibilityCase, field: keyof CompatibilityCase, value: string) {
  const updated = { ...item, [field]: value };
  const result = await api<{ ok: boolean; cases?: CompatibilityCase[]; error?: string }>(
    "POST",
    "/bottles/record-compatibility",
    {
      id: item.id,
      installerOpens: updated.installer_opens,
      finalAppDetected: updated.final_app_detected,
      finalAppLaunches: updated.final_app_launches,
      knownMissingRuntime: updated.known_missing_runtime,
      notes: updated.notes ?? "",
    },
  );
  if (result?.ok && result.cases) {
    compatibilityCases.value = result.cases;
    toast.show("Compatibility evidence recorded", "success");
  } else {
    toast.show(result?.error ?? "Failed to record compatibility evidence", "error");
  }
}

async function openRedistSource(source: RedistSourceGuide) {
  await getAPI().copyText(source.source_url);
  toast.show(`${source.name} source URL copied`, "success");
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
    runningSharpPids.value[id] = result.pid;
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

async function stopSharpApp(app: SharpApp) {
  const pid = runningSharpPids.value[app.id];
  if (!pid) return;
  await api("POST", "/kill", { pid });
  delete runningSharpPids.value[app.id];
  toast.show(`Closed ${app.name}`);
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

async function openBottleLog(bottle: BottleManifest) {
  if (!bottle.last_launch_log) {
    toast.show("No bottle launch log recorded yet", "warning");
    return;
  }
  await getAPI().openInFinder(bottle.last_launch_log);
}

async function openBottleFolder(bottle: BottleManifest) {
  await getAPI().openInFinder(bottle.prefix_path);
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

function closeDropdowns(e: MouseEvent) {
  if (!(e.target as HTMLElement).closest('.dropdown-wrap')) dropdownOpen.value = null;
}
onMounted(() => { document.addEventListener('click', closeDropdowns); load(); });
onUnmounted(() => { document.removeEventListener('click', closeDropdowns); });
</script>

<template>
  <div class="sharp-view">
    <div class="sharp-header">
      <h1>Sharp Library</h1>
      <div class="sharp-header-controls">
      <div v-if="bottles.length" class="dropdown-wrap">
          <button class="btn btn-secondary" @click="openDropdown('bottles', $event)">
            <IconGlassWater class="btn-icon" width="14" height="14" style="transform:rotate(45deg)" />
            <span class="btn-label-long">Runtime Bottles</span><span class="btn-label-short">Bottles</span> <span class="dropdown-count">{{ bottles.length }}</span>
        </button>
        <div v-if="dropdownOpen === 'bottles'" class="dropdown-panel" :style="dropdownStyle" @click.stop>
          <div class="dropdown-scroll">
            <article v-for="bottle in bottles" :key="bottle.id" class="bottle-card-compact">
              <div class="bottle-card-main">
                <div class="bottle-identity">
                  <div class="bottle-title">{{ bottle.name }}</div>
                  <div class="bottle-meta">
                    <span class="badge" :class="bottleBadgeClass(bottle.health)">{{ bottle.health }}</span>
                    <span v-if="bottle.last_launch_status">
                      {{ bottle.last_launch_status }}
                      <template v-if="bottle.last_launch_pid">pid {{ bottle.last_launch_pid }}</template>
                    </span>
                  </div>
                </div>
                <div class="bottle-facts">
                  <span><strong>Kind</strong> {{ bottle.bottle_type }}</span>
                  <span><strong>Runtime</strong> {{ bottle.runtime_profile }}</span>
                  <span><strong>Arch</strong> {{ bottle.arch }}</span>
                  <span v-if="bottle.steam_app_id"><strong>Steam</strong> {{ bottle.steam_app_id }}</span>
                </div>
                <div class="bottle-actions">
                  <button class="btn btn-secondary btn-sm" :disabled="bottleLoading[bottle.id]" @click="doctorBottle(bottle.id)">
                    {{ bottleLoading[bottle.id] ? "Checking" : "Doctor" }}
                  </button>
                  <button class="btn btn-secondary btn-sm" :disabled="bottleLoading[bottle.id]" @click="refreshBottle(bottle.id)">Scan</button>
                  <button class="btn btn-secondary btn-sm" @click="bottleAdvancedOpen[bottle.id] = !bottleAdvancedOpen[bottle.id]">
                    {{ bottleAdvancedOpen[bottle.id] ? "Less" : "More" }}
                  </button>
                </div>
              </div>
              <div v-if="bottleAdvancedOpen[bottle.id]" class="bottle-control-surface">
                <div class="bottle-control-grid">
                  <button class="btn btn-secondary btn-sm" :disabled="bottleLoading[bottle.id]" @click="prepareBottle(bottle.id)">Prepare</button>
                  <button v-if="bottle.source_installer_path" class="btn btn-secondary btn-sm" :disabled="bottleLoading[bottle.id]" @click="relaunchBottleInstaller(bottle)">Relaunch</button>
                  <button class="btn btn-secondary btn-sm" @click="openBottleFolder(bottle)">Folder</button>
                  <button class="btn btn-secondary btn-sm" :disabled="!bottle.last_launch_log" @click="openBottleLog(bottle)">Logs</button>
                  <select class="control-input" :value="bottle.runtime_profile" :disabled="bottleLoading[bottle.id]" @change="setBottleProfile(bottle.id, ($event.target as HTMLSelectElement).value)">
                    <option v-for="profile in visibleRuntimeProfiles" :key="profile.id" :value="profile.id">{{ profile.name }}</option>
                  </select>
                  <div class="windows-version-controls">
                    <button class="btn btn-secondary btn-sm" :disabled="bottleLoading[bottle.id]" @click="setBottleWindowsVersion(bottle.id, 'win7')">Win7</button>
                    <button class="btn btn-secondary btn-sm" :disabled="bottleLoading[bottle.id]" @click="setBottleWindowsVersion(bottle.id, 'win10')">Win10</button>
                    <button class="btn btn-secondary btn-sm" :disabled="bottleLoading[bottle.id]" @click="setBottleWindowsVersion(bottle.id, 'win11')">Win11</button>
                  </div>
                </div>
                <div class="bottle-components">
                  <template v-if="isFnaProfile(bottle.runtime_profile)">
                    <div class="fna-component-header">Mono/FNA Runtime</div>
                    <span v-for="component in bottle.installed_components.filter(c => fnaComponentIds.has(c.id))" :key="component.id" class="component-pill" :class="componentStateClass(component.state)">{{ componentLabel(component.id) }}: {{ component.state }}</span>
                    <span v-for="component in bottle.installed_components.filter(c => !fnaComponentIds.has(c.id))" :key="component.id" class="component-pill">{{ componentLabel(component.id) }}: {{ component.state }}</span>
                  </template>
                  <template v-else>
                    <span v-for="component in bottle.installed_components" :key="component.id" class="component-pill" :class="componentStateClass(component.state)">{{ componentLabel(component.id) }}: {{ component.state }}</span>
                  </template>
                  <span v-if="bottle.runtime_assets?.length" class="component-pill">runtime assets: {{ bottle.runtime_assets.length }}</span>
                </div>
                <div v-if="bottle.installed_app_detections?.length" class="bottle-detections">
                  <button v-for="candidate in bottle.installed_app_detections.slice(0, 3)" :key="candidate.exe_path" class="btn btn-secondary btn-sm" :disabled="bottleLoading[bottle.id]" @click="addBottleApp(bottle, candidate)">Add {{ candidate.name }}</button>
                </div>
              </div>
              <div v-if="bottleReports[bottle.id]" class="bottle-report">
                <div class="doctor-summary">
                  <span class="badge" :class="bottleReports[bottle.id]?.ready ? 'badge-ok' : 'badge-warn'">{{ bottleReports[bottle.id]?.ready ? "Ready" : "Repair" }}</span>
                  <span>{{ bottleReports[bottle.id]?.summary }}</span>
                </div>
                <div v-if="bottleReports[bottle.id]?.actions.length" class="doctor-notes blocked">
                  <div v-for="action in bottleReports[bottle.id]?.actions" :key="action.id" class="bottle-action-row">
                    <span>{{ componentLabel(action.id) }}: {{ action.detail }}</span>
                    <button class="btn btn-secondary btn-sm" :disabled="bottleLoading[bottle.id]" @click="repairBottleComponent(bottle.id, action.id)">Repair</button>
                  </div>
                </div>
                <div v-if="bottleReports[bottle.id]?.component_sources?.length" class="doctor-notes">
                  <div v-for="source in bottleReports[bottle.id]?.component_sources" :key="source.id" class="component-source-row">
                    <span class="source-label">{{ componentLabel(source.id) }}</span>
                    <span class="source-status" :class="source.available ? 'source-ok' : 'source-missing'">{{ source.available ? source.source : "missing source" }}</span>
                    <span v-if="source.detail" class="source-detail">{{ source.detail }}</span>
                  </div>
                </div>
              </div>
            </article>
          </div>
        </div>
      </div>
      <div v-if="compatibilityCases.length" class="dropdown-wrap">
          <button class="btn btn-secondary" @click="openDropdown('compat', $event)">
            <IconLayoutGrid class="btn-icon" width="14" height="14" />
            <span class="btn-label-long">Compatibility</span><span class="btn-label-short">Compat</span> <span class="dropdown-count">{{ compatibilityCases.length }}</span>
        </button>
        <div v-if="dropdownOpen === 'compat'" class="dropdown-panel" :style="dropdownStyle" @click.stop>
          <div class="dropdown-scroll">
            <div class="compatibility-row compatibility-header">
              <span>Case</span><span>Profile</span><span>Installer</span><span>Detected</span><span>Launch</span><span>Runtime</span><span>Prefix</span>
            </div>
            <div v-for="item in compatibilityCases" :key="item.id" class="compatibility-row">
              <span><strong>{{ item.name }}</strong><small>{{ item.case_type }}<template v-if="item.bottle_id"> · {{ item.bottle_id }}</template></small></span>
              <span>{{ item.required_profile }}</span>
              <select class="compatibility-select" :value="item.installer_opens" @change="recordCompatibility(item, 'installer_opens', ($event.target as HTMLSelectElement).value)">
                <option value="untested">untested</option><option value="needs_real_trace">needs trace</option><option value="yes">yes</option><option value="no">no</option><option value="not_applicable">n/a</option>
              </select>
              <select class="compatibility-select" :value="item.final_app_detected" @change="recordCompatibility(item, 'final_app_detected', ($event.target as HTMLSelectElement).value)">
                <option value="pending">pending</option><option value="yes">yes</option><option value="no">no</option><option value="not_applicable">n/a</option>
              </select>
              <select class="compatibility-select" :value="item.final_app_launches" @change="recordCompatibility(item, 'final_app_launches', ($event.target as HTMLSelectElement).value)">
                <option value="pending">pending</option><option value="unknown">unknown</option><option value="yes">yes</option><option value="no">no</option><option value="not_applicable">n/a</option>
              </select>
              <input class="compatibility-input" :value="item.known_missing_runtime" @change="recordCompatibility(item, 'known_missing_runtime', ($event.target as HTMLInputElement).value)" />
              <span>{{ item.per_game_prefix_recommendation }}<small v-if="item.evidence_updated_at">updated {{ item.evidence_updated_at }}</small></span>
            </div>
          </div>
        </div>
      </div>
      <div v-if="redistSources.length" class="dropdown-wrap">
          <button class="btn btn-secondary" @click="openDropdown('redist', $event)">
            <IconDownload class="btn-icon" width="14" height="14" />
            <span class="btn-label-long">Redist Sources</span><span class="btn-label-short">Redist</span> <span class="dropdown-count">{{ redistSources.length }}</span>
        </button>
        <div v-if="dropdownOpen === 'redist'" class="dropdown-panel" :style="dropdownStyle" @click.stop>
          <div class="dropdown-scroll">
            <article v-for="source in redistSources" :key="source.id" class="redist-source-compact">
              <div><strong>{{ source.name }}</strong><small>{{ source.policy }}</small></div>
              <p>{{ source.notes }}</p>
              <div class="redist-targets"><span v-for="target in source.local_targets" :key="target">{{ target }}</span></div>
              <button class="btn btn-secondary btn-sm" @click="openRedistSource(source)">Copy Source URL</button>
            </article>
          </div>
        </div>
      </div>
      <button class="btn btn-primary" @click="installExe">
        <IconUpload class="btn-icon" width="14" height="14" />
        <span class="btn-label-long">Install Windows Program</span><span class="btn-label-short">Install</span>
      </button>
      <button class="btn btn-secondary" @click="refreshSharpLibrary">
        <IconRefreshCcw class="btn-icon" width="14" height="14" />
        <span class="btn-label-long">Refresh</span><span class="btn-label-short">Refresh</span>
      </button>
      </div>
    </div>

    <div v-if="apps.length === 0" class="empty-state">
      <div class="empty-icon">
        <IconMonitor width="48" height="48" />
      </div>
      <h2>No applications installed</h2>
      <p>Click "Install Windows Program" to add a Windows application</p>
    </div>

    <div v-else class="sharp-grid">
      <div v-for="app in apps" :key="app.id" class="sharp-card" :class="{ running: runningSharpPids[app.id] }">
        <div class="sharp-card-banner">
          <img
            v-if="app.cover"
            :src="`http://127.0.0.1:9274/sharp-library/cover?id=${app.id}`"
            :alt="app.name"
            :style="{ objectPosition: coverPosition(app) }"
          />
          <span v-else class="sharp-icon-placeholder">{{ app.name.charAt(0) }}</span>
          <button
            v-if="runningSharpPids[app.id]"
            class="running-close-button"
            title="Close application"
            @click="stopSharpApp(app)"
          >
            <IconX width="14" height="14" />
          </button>
        </div>
        <div class="sharp-card-body">
          <div class="sharp-card-title">{{ app.name }}</div>
          <div class="sharp-card-meta">
            <span class="badge badge-ok">Sharp App</span>
            <span class="sharp-card-size">{{ formatBytes(app.size_bytes) }}</span>
          </div>
          <div class="sharp-card-actions">
            <div class="sharp-card-actions-row">
              <button v-if="runningSharpPids[app.id]" class="btn btn-stop" @click="stopSharpApp(app)">Stop</button>
              <button v-else class="btn btn-play" @click="launchApp(app.id, app.engine)">Play</button>
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
            <details class="sharp-card-tools">
              <summary class="drawer-summary">
                <span>Tools</span>
                <small>cover, launch args, diagnostics</small>
              </summary>
              <div class="sharp-tool-actions">
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
              <button class="btn btn-danger btn-sm sharp-uninstall-button" @click="uninstallApp(app.id)">
                Uninstall
              </button>
            </details>
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
  padding: 0 28px 32px;
  height: 100%;
  overflow-y: auto;
}
.sharp-header {
  display: flex;
  flex-direction: column;
  gap: 18px;
  margin: 0 -28px 20px;
  padding: 44px 28px 13px;
  background: var(--page-header-bg);
  border-bottom: 1px solid var(--border);
  -webkit-app-region: drag;
  position: relative;
}
.sharp-header::after {
  content: "";
  position: absolute;
  inset: 0;
  background: radial-gradient(ellipse 60% 80% at 20% 50%, rgba(95, 183, 232, 0.08) 0%, transparent 70%),
              radial-gradient(ellipse 40% 60% at 80% 50%, rgba(95, 183, 232, 0.05) 0%, transparent 60%);
  pointer-events: none;
}
.sharp-header-controls {
  display: flex;
  align-items: center;
  gap: 8px;
  flex-wrap: wrap;
  -webkit-app-region: no-drag;
  margin-top: auto;
  padding-top: 25px;
  min-height: 0;
  container-type: inline-size;
}
.btn-label-short { display: none; }
@container (max-width: 700px) {
  .btn-label-long { display: none; }
  .btn-label-short { display: inline; }
}
.sharp-header-controls .btn {
  min-width: 0;
  flex-shrink: 1;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.btn-icon {
  flex-shrink: 0;
}
.sharp-header h1 {
  font-size: 24px;
  font-weight: 750;
  line-height: 1.1;
}
.sharp-header-controls {
  display: flex;
  align-items: center;
  gap: 8px;
  flex-wrap: nowrap;
  overflow-x: auto;
  -webkit-app-region: no-drag;
}

.support-drawer {
  margin-bottom: 10px;
  border: 1px solid var(--border);
  border-radius: var(--radius-md);
  background: color-mix(in srgb, var(--bg-card) 82%, transparent);
  overflow: hidden;
}
.dropdown-wrap {
  position: relative;
  min-width: 0;
  flex-shrink: 1;
  overflow: hidden;
}
.dropdown-count {
  opacity: 0.5;
  font-size: 11px;
  margin-left: 2px;
}
.dropdown-panel {
  position: fixed;
  max-height: min(60vh, 520px);
  background: var(--bg-card);
  border: 1px solid var(--border);
  border-radius: var(--radius-md);
  box-shadow: 0 12px 40px rgba(0, 0, 0, 0.3);
  z-index: 100;
  overflow: hidden;
}
.dropdown-scroll {
  overflow-y: auto;
  max-height: min(60vh, 520px);
  padding: 8px;
}
.bottle-card-compact {
  padding: 10px 12px;
  border: 1px solid var(--border);
  border-radius: var(--radius-sm);
  margin-bottom: 6px;
  background: var(--bg-surface);
}
.bottle-card-compact:last-child {
  margin-bottom: 0;
}
.redist-source-compact {
  padding: 10px 12px;
  border: 1px solid var(--border);
  border-radius: var(--radius-sm);
  margin-bottom: 6px;
  background: var(--bg-surface);
  font-size: 12px;
}
.redist-source-compact:last-child {
  margin-bottom: 0;
}
.bottle-list {
  display: flex;
  flex-direction: column;
  gap: 8px;
}
.bottle-card-main {
  display: grid;
  grid-template-columns: minmax(170px, 1.2fr) minmax(300px, 1.8fr) auto;
  align-items: center;
  gap: 14px;
}
.bottle-identity {
  min-width: 0;
}
.bottle-title {
  max-width: 100%;
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
.bottle-facts {
  display: grid;
  grid-template-columns: repeat(4, minmax(0, 1fr));
  gap: 8px;
  min-width: 0;
}
.bottle-facts span {
  min-width: 0;
  color: var(--text-secondary);
  font-size: 11px;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.bottle-facts strong {
  display: block;
  margin-bottom: 2px;
  color: var(--text-dim);
  font-size: 9px;
  font-weight: 700;
  text-transform: uppercase;
}
.bottle-actions {
  display: flex;
  flex-wrap: wrap;
  justify-content: flex-end;
  gap: 6px;
}
.bottle-control-surface {
  margin-top: 8px;
  border: 1px solid var(--border);
  border-radius: var(--radius-sm);
  background: color-mix(in srgb, var(--bg-surface) 82%, transparent);
}
.bottle-control-grid {
  display: grid;
  grid-template-columns: repeat(4, minmax(0, max-content)) minmax(190px, 1fr) auto;
  gap: 8px;
  padding: 9px;
}
.bottle-control-grid .control-input {
  min-width: 0;
}
.windows-version-controls {
  display: flex;
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
.component-pill.pill-ok {
  color: var(--color-green, #4ade80);
  border-color: var(--color-green, #4ade80);
}
.component-pill.pill-missing {
  color: var(--color-red, #f87171);
  border-color: var(--color-red, #f87171);
}
.component-pill.pill-warn {
  color: var(--color-yellow, #facc15);
  border-color: var(--color-yellow, #facc15);
}
.fna-component-header {
  font-size: 11px;
  font-weight: 600;
  text-transform: uppercase;
  letter-spacing: 0.5px;
  color: var(--text-secondary);
  margin-bottom: 4px;
  grid-column: 1 / -1;
}
.component-source-row {
  display: flex;
  flex-wrap: wrap;
  gap: 6px;
  align-items: baseline;
  margin-top: 4px;
}
.source-label {
  font-weight: 600;
  min-width: 110px;
}
.source-ok {
  color: var(--color-green, #4ade80);
}
.source-missing {
  color: var(--color-red, #f87171);
}
.source-detail {
  color: var(--text-secondary);
  font-size: 11px;
  flex-basis: 100%;
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
@media (max-width: 980px) {
  .bottle-card-main {
    grid-template-columns: minmax(0, 1fr);
    align-items: start;
  }
  .bottle-facts {
    grid-template-columns: repeat(2, minmax(0, 1fr));
  }
  .bottle-actions {
    justify-content: flex-start;
  }
  .bottle-control-grid {
    grid-template-columns: repeat(2, minmax(0, 1fr));
  }
}
.compatibility-table {
  border: 1px solid var(--border);
  border-radius: var(--radius-md);
  overflow: hidden;
  background: var(--bg-card);
}
.compatibility-row {
  display: grid;
  grid-template-columns: minmax(170px, 1.3fr) 100px 92px 92px 92px minmax(150px, 1fr) minmax(130px, 0.9fr);
  gap: 10px;
  align-items: center;
  padding: 8px 10px;
  border-top: 1px solid var(--border);
  color: var(--text-secondary);
  font-size: 11px;
}
.compatibility-row:first-child {
  border-top: 0;
}
.compatibility-header {
  color: var(--text-dim);
  font-weight: 700;
  text-transform: uppercase;
}
.compatibility-row strong,
.compatibility-row small {
  display: block;
}
.compatibility-row small {
  margin-top: 2px;
  color: var(--text-dim);
}
.compatibility-select,
.compatibility-input {
  min-width: 0;
  width: 100%;
  height: 28px;
  border: 1px solid var(--border);
  border-radius: var(--radius-sm);
  background: var(--bg-input);
  color: var(--text-secondary);
  font-size: 11px;
}
.compatibility-input {
  padding: 4px 7px;
}
.redist-source-list {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(280px, 1fr));
  gap: 10px;
}
.redist-source-card {
  padding: 12px;
  border: 1px solid var(--border);
  border-radius: var(--radius-md);
  background: var(--bg-card);
}
.redist-source-card strong,
.redist-source-card small {
  display: block;
}
.redist-source-card small,
.redist-source-card p {
  margin-top: 4px;
  color: var(--text-dim);
  font-size: 11px;
  line-height: 1.4;
}
.redist-targets {
  display: flex;
  flex-direction: column;
  gap: 4px;
  margin: 8px 0;
}
.redist-targets span {
  padding: 4px 6px;
  border: 1px solid var(--border);
  border-radius: var(--radius-sm);
  color: var(--text-secondary);
  font-size: 10px;
  overflow-wrap: anywhere;
}

.sharp-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(240px, 1fr));
  gap: 12px;
  align-items: start;
}

.sharp-card {
  align-self: start;
  height: fit-content;
  background: var(--bg-card);
  border: 1px solid var(--border);
  border-radius: var(--radius-md);
  overflow: hidden;
  box-shadow:
    0 0 0 1px color-mix(in srgb, var(--accent) 22%, transparent),
    0 0 26px color-mix(in srgb, var(--accent) 20%, transparent),
    0 16px 34px color-mix(in srgb, var(--bg-deep) 34%, transparent);
  transition:
    transform var(--transition),
    border-color var(--transition),
    box-shadow var(--transition);
}
.sharp-card:hover {
  border-color: var(--border-strong);
  transform: translateY(-1px);
  box-shadow:
    0 0 0 1px color-mix(in srgb, var(--accent) 36%, transparent),
    0 0 34px color-mix(in srgb, var(--accent) 28%, transparent),
    0 20px 42px color-mix(in srgb, var(--bg-deep) 42%, transparent);
}
.sharp-card.running {
  border-color: var(--success);
  box-shadow:
    0 0 0 1px color-mix(in srgb, var(--success) 48%, transparent),
    0 0 34px color-mix(in srgb, var(--success) 28%, transparent),
    0 20px 42px color-mix(in srgb, var(--bg-deep) 42%, transparent);
}
.sharp-card-banner {
  width: 100%;
  aspect-ratio: 16 / 5.6;
  height: auto;
  background: var(--bg-surface);
  display: flex;
  align-items: center;
  justify-content: center;
  overflow: hidden;
  position: relative;
}
.sharp-card-banner img {
  width: 100%;
  height: 100%;
  object-fit: cover;
}
.sharp-icon-placeholder {
  font-size: 32px;
  font-weight: 700;
  color: var(--text-dim);
  opacity: 0.34;
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
.sharp-card-body {
  padding: 11px 12px 12px;
}
.sharp-card-title {
  font-size: 14px;
  font-weight: 600;
  margin-bottom: 5px;
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
  gap: 7px;
}
.sharp-card-actions-row {
  display: flex;
  align-items: center;
  gap: 8px;
}
.sharp-card-actions-row .btn-play,
.sharp-card-actions-row .btn-stop {
  min-width: 58px;
}
.sharp-card-tools {
  border: 1px solid var(--border);
  border-radius: var(--radius-sm);
  background: color-mix(in srgb, var(--bg-surface) 72%, transparent);
}
.sharp-card-tools .drawer-summary {
  padding: 8px;
}
.sharp-card-tools[open] {
  padding-bottom: 8px;
}
.sharp-tool-actions {
  display: grid;
  grid-template-columns: repeat(3, minmax(0, 1fr));
  gap: 6px;
  padding: 0 8px 8px;
}
.sharp-tool-actions .btn {
  min-width: 0;
  padding-inline: 6px;
}
.cover-position-controls {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 7px;
  padding: 0 8px 8px;
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
  gap: 6px;
  padding: 0 8px 8px;
}
.launch-options-input {
  width: 100%;
}
.sharp-uninstall-button {
  margin: 0 8px;
  width: calc(100% - 16px);
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
