<script setup lang="ts">
import { computed, ref, onMounted, watch } from "vue";
import { useToast } from "../composables/useToast";
import { api } from "../composables/useApi";
import sharpLogoUrl from "../icon.png";
import IconX from "~icons/lucide/x";
import IconFlaskConical from "~icons/lucide/flask-conical";
import IconShieldPlus from "~icons/lucide/shield-plus";
import IconBox from "~icons/lucide/box";
import IconRotateCcw from "~icons/lucide/rotate-ccw";

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

interface D3DMetalGptkAction {
  id: string;
  label: string;
  enabled: boolean;
  state: string;
  detail: string;
}

interface D3DMetalGptkState {
  bottle_id: string;
  appid: number;
  name: string;
  game_dir: string;
  game_exe?: string | null;
  gptk_homebrew: string;
  rosetta: string;
  gptk_payload: string;
  x64_redist: string;
  seed: string;
  play_ready: boolean;
  last_error?: string | null;
}

interface D3DMetalLaunchReport {
  pid?: number;
  appid?: number;
  bottle_id?: string;
  game_exe?: string;
  log_path?: string;
}

interface D3DMetalGptkResponse {
  ok: boolean;
  state?: D3DMetalGptkState;
  actions?: D3DMetalGptkAction[];
  launch?: D3DMetalLaunchReport;
  error?: string;
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
  d3dmetalLaunched: [pid: number];
  stop: [];
  install: [];
  uninstall: [];
  expanded: [appid: number, open: boolean];
  artworkMissing: [appid: number];
}>();

const toast = useToast();
const goldbergActive = ref(false);
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
const d3dmetalState = ref<D3DMetalGptkState | null>(null);
const d3dmetalActions = ref<D3DMetalGptkAction[]>([]);
const d3dmetalLoading = ref(false);
const bottleName = ref("");
const bottlePreferredMode = ref("auto");
const bottleSaving = ref(false);
const artworkLoadFailed = ref(false);
const launchModeStorageKey = computed(() => `metalsharp-launch-mode-${props.game.appid}`);
const userSelectablePipelineOrder = ["d3dmetal", "m12", "m11", "m11_32", "m10", "m10_32", "m9", "fna_arm64"];
const userSelectablePipelineNames: Record<string, string> = {
  m12: "M12",
  d3dmetal: "D3DMetal",
  m11: "M11",
  m11_32: "M11(32)",
  m10: "M10",
  m10_32: "M10(32)",
  m9: "M9",
  fna_arm64: "Mono/FNA",
};

const componentDisplayName: Record<string, string> = {
  "mono-arm64": "Mono ARM64",
  "mono-x86": "Mono x86_64",
  "fna": "FNA Runtime",
  "xna": "XNA Assemblies",
  "sdl2": "SDL2",
  "fna3d": "FNA3D",
  "faudio": "FAudio",
  "fmod": "FMOD Audio",
  "m12_d3d12": "M12 d3d12.dll",
  "m12_d3d11": "M12 d3d11.dll",
  "m12_d3d10core": "M12 d3d10core.dll",
  "m12_dxgi_dxmt": "M12 dxgi_dxmt.dll",
  "m12_dxgi": "M12 dxgi.dll",
  "m12_winemetal": "M12 winemetal.dll / .so",
  "m12_gpu_stubs": "M12 GPU Stubs",
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
};

const runtimeProfileDisplayName: Record<string, string> = {
  fna_arm64: "FNA / Mono ARM64",
  fna_x86: "FNA / Mono x86_64",
  d3dmetal: "D3DMetal (GPTK)",
  game_install: "Game Installer",
  winebare: "Plain Wine",
};

function componentLabel(id: string): string {
  return componentDisplayName[id] ?? id;
}

function runtimeProfileLabel(id: string): string {
  return runtimeProfileDisplayName[id] ?? id;
}

function componentStateIcon(state: string): string {
  if (state === "installed" || state === "ready") return "OK";
  if (state === "missing") return "!";
  if (state === "needs_repair" || state === "partial") return "~";
  return "?";
}

function componentStateClass(state: string): string {
  if (state === "installed" || state === "ready") return "check-ok";
  if (state === "missing") return "check-missing";
  if (state === "needs_repair" || state === "partial") return "check-warn";
  return "check-unknown";
}

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

const primaryArtworkUrl = computed(() => props.game.header_url || props.game.cover_url || "");
const displayedArtworkUrl = computed(() =>
  primaryArtworkUrl.value && !artworkLoadFailed.value ? primaryArtworkUrl.value : sharpLogoUrl,
);
const usingFallbackArtwork = computed(() => !primaryArtworkUrl.value || artworkLoadFailed.value);

const bottlePipelineOptions = computed(() =>
  userSelectablePipelineOrder.map((id) => ({ id, name: userSelectablePipelineNames[id] })),
);

function preferredBottlePipeline(report: SteamRuntimeReport) {
  const candidates = [report.preferred_pipeline, report.pipeline, report.runtime_profile, props.game.preferred_pipeline, props.game.launch_method, selectedLaunchMode.value];
  return candidates.find((id) => id && userSelectablePipelineOrder.includes(id)) ?? "m11";
}

function runtimeDoctorPipelineRequest() {
  if (selectedLaunchMode.value === "d3dmetal" || bottlePreferredMode.value === "d3dmetal") return "auto";
  return selectedLaunchMode.value === "auto" ? "auto" : selectedLaunchMode.value;
}

function reportIsD3DMetal(report: SteamRuntimeReport | null): report is SteamRuntimeReport {
  return report?.preferred_pipeline === "d3dmetal" || report?.runtime_profile === "d3dmetal";
}

function isD3DMetalBottleSelected() {
  if (bottlePreferredMode.value !== "auto") return bottlePreferredMode.value === "d3dmetal";
  if (runtimeReport.value) return reportIsD3DMetal(runtimeReport.value);
  return props.game.preferred_pipeline === "d3dmetal" || props.game.launch_method === "d3dmetal";
}

function hasSavedD3DMetalRoute() {
  if (bottlePreferredMode.value !== "auto") return bottlePreferredMode.value === "d3dmetal";
  if (runtimeReport.value) return reportIsD3DMetal(runtimeReport.value);
  return props.game.preferred_pipeline === "d3dmetal" || props.game.launch_method === "d3dmetal";
}

function d3dmetalComponentState(state: string) {
  if (["installed", "updated", "seeded"].includes(state)) return "installed";
  if (["failed", "repair_required"].includes(state)) return "needs_repair";
  if (["installing", "updating", "seeding"].includes(state)) return "partial";
  return state;
}

function runtimeReportFromD3DMetalState(state: D3DMetalGptkState, actions: D3DMetalGptkAction[]): SteamRuntimeReport {
  const requiredActions = state.play_ready
    ? []
    : actions
        .filter((action) => action.id !== "play_d3dmetal")
        .map((action) => ({ id: action.id, status: d3dmetalComponentState(action.state), detail: action.detail }));
  return {
    appid: state.appid,
    bottle_id: state.bottle_id,
    bottle_name: state.name,
    preferred_pipeline: "d3dmetal",
    pipeline: "d3dmetal",
    runtime_profile: "d3dmetal",
    prefix_path: "Homebrew GPTK",
    game_install_path: state.game_dir,
    runtime_assets: [],
    components: [
      { id: "gptk", state: d3dmetalComponentState(state.gptk_payload) },
      { id: "rosetta", state: d3dmetalComponentState(state.rosetta) },
      { id: "vcrun2019_x64", state: d3dmetalComponentState(state.x64_redist) },
      { id: "vcrun2019_x86", state: d3dmetalComponentState(state.x64_redist) },
      { id: "gptk_prefix", state: d3dmetalComponentState(state.seed) },
    ],
    actions: requiredActions,
  };
}

const visibleD3DMetalActions = computed(() =>
  d3dmetalActions.value.filter((action) => action.id !== "play_d3dmetal"),
);

function d3dmetalActionReady(action: D3DMetalGptkAction) {
  return ["installed", "updated", "seeded"].includes(action.state);
}

function clearD3DMetalPanelState() {
  d3dmetalState.value = null;
  d3dmetalActions.value = [];
}

function syncD3DMetalRuntimeReport() {
  if (d3dmetalState.value && runtimeReport.value?.runtime_profile === "d3dmetal" && isD3DMetalBottleSelected()) {
    runtimeReport.value = runtimeReportFromD3DMetalState(d3dmetalState.value, d3dmetalActions.value);
  }
}

function effectivePlayLaunchMode() {
  if (selectedLaunchMode.value !== "auto") return selectedLaunchMode.value;
  return hasSavedD3DMetalRoute() ? "d3dmetal" : "auto";
}

onMounted(async () => {
  if (!primaryArtworkUrl.value) emit("artworkMissing", props.game.appid);

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

  }
});

watch(primaryArtworkUrl, (url) => {
  artworkLoadFailed.value = false;
  if (!url) emit("artworkMissing", props.game.appid);
});

function onArtworkError() {
  if (!primaryArtworkUrl.value || artworkLoadFailed.value) return;
  artworkLoadFailed.value = true;
  emit("artworkMissing", props.game.appid);
}

watch(doctorOpen, (open) => {
  if (!open) emit('expanded', props.game.appid, false);
});
watch(runtimeOpen, (open) => {
  if (!open) emit('expanded', props.game.appid, false);
});

watch(selectedLaunchMode, (mode) => {
  localStorage.setItem(launchModeStorageKey.value, mode);
});

watch(bottlePreferredMode, (mode) => {
  if (mode !== "d3dmetal") {
    clearD3DMetalPanelState();
    if (reportIsD3DMetal(runtimeReport.value)) {
      runtimeReport.value = {
        ...runtimeReport.value,
        preferred_pipeline: mode,
        pipeline: mode,
        runtime_profile: mode,
        components: [],
        actions: [],
      };
    }
  }
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

async function runDoctor() {
  doctorOpen.value = true;
  emit('expanded', props.game.appid, true);
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
  emit('expanded', props.game.appid, true);
  runtimeLoading.value = true;
  const savedD3DMetalRoute = hasSavedD3DMetalRoute();
  runtimeReport.value = null;
  if (savedD3DMetalRoute) {
    await loadD3DMetalStatus();
    if (d3dmetalState.value) {
      runtimeReport.value = runtimeReportFromD3DMetalState(d3dmetalState.value, d3dmetalActions.value);
      bottleName.value = d3dmetalState.value.name || props.game.name;
      bottlePreferredMode.value = "d3dmetal";
    } else {
      toast.show("D3DMetal state not found; save the D3DMetal bottle again", "error");
    }
    runtimeLoading.value = false;
    return;
  }
  clearD3DMetalPanelState();
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
    if (isD3DMetalBottleSelected()) {
      await loadD3DMetalStatus();
    } else {
      clearD3DMetalPanelState();
    }
  } else {
    toast.show(result?.error ?? "Runtime Doctor failed", "error");
  }
}

async function openBottleWorkspace() {
  if (runtimeOpen.value && runtimeReport.value) {
    runtimeOpen.value = false;
    emit('expanded', props.game.appid, false);
    return;
  }
  await runRuntimeDoctor();
}

async function loadD3DMetalStatus() {
  const bottleId = runtimeReport.value?.bottle_id ?? props.game.bottle_id ?? `steam_${props.game.appid}`;
  const result = await api<D3DMetalGptkResponse>("POST", "/d3dmetal/bottles/status", {
    appid: props.game.appid,
    bottleId,
  });
  if (result?.ok && result.state) {
    d3dmetalState.value = result.state;
    d3dmetalActions.value = result.actions ?? [];
    syncD3DMetalRuntimeReport();
  } else {
    d3dmetalState.value = null;
    d3dmetalActions.value = [];
  }
}

async function playSelectedLaunchMode() {
  const launchMode = effectivePlayLaunchMode();
  if (launchMode !== "d3dmetal") {
    emit('play', launchMode);
    return;
  }
  if (!d3dmetalState.value) await loadD3DMetalStatus();
  const playAction = d3dmetalActions.value.find((action) => action.id === "play_d3dmetal") ?? {
    id: "play_d3dmetal",
    label: "Play D3DMetal",
    enabled: d3dmetalState.value?.play_ready === true,
    state: d3dmetalState.value?.play_ready ? "seeded" : "missing",
    detail: "Launch game exe directly through GPTK Wine",
  };
  if (!d3dmetalState.value?.play_ready || !playAction.enabled) {
    toast.show("D3DMetal bottle is not ready; seed VC runtime DLLs and seed prefix first", "error");
    return;
  }
  const pid = await runD3DMetalAction(playAction);
  if (pid) emit('d3dmetalLaunched', pid);
}

async function runD3DMetalPanelAction(action: D3DMetalGptkAction) {
  const pid = await runD3DMetalAction(action);
  if (action.id === "play_d3dmetal" && pid) emit('d3dmetalLaunched', pid);
}

function d3dmetalActionRoute(actionId: string) {
  switch (actionId) {
    case "install_homebrew_gptk":
      return "/d3dmetal/bottles/install-homebrew-gptk";
    case "install_rosetta":
      return "/d3dmetal/bottles/install-rosetta";
    case "repair_gptk_payload":
      return "/d3dmetal/bottles/repair-gptk-payload";
    case "install_x64_redist":
      return "/d3dmetal/bottles/install-x64-redist";
    case "seed_prefix":
      return "/d3dmetal/bottles/seed-prefix";
    default:
      return "/d3dmetal/bottles/play";
  }
}

async function runD3DMetalAction(action: D3DMetalGptkAction): Promise<number | null> {
  const bottleId = runtimeReport.value?.bottle_id ?? props.game.bottle_id ?? `steam_${props.game.appid}`;
  const gameDir = runtimeReport.value?.game_install_path;
  d3dmetalLoading.value = true;
  const route = d3dmetalActionRoute(action.id);
  const result = await api<D3DMetalGptkResponse>("POST", route, {
    appid: props.game.appid,
    bottleId,
    gameDir,
  }, 10 * 60 * 1000);
  d3dmetalLoading.value = false;
  if (result?.ok) {
    toast.show(action.id === "play_d3dmetal" ? "D3DMetal launch started" : `${action.label}: complete`, "success");
    if (result.state) {
      d3dmetalState.value = result.state;
      d3dmetalActions.value = result.actions ?? [];
      syncD3DMetalRuntimeReport();
    } else {
      await loadD3DMetalStatus();
    }
    return result.launch?.pid ?? null;
  } else {
    toast.show(result?.error ?? `${action.label} failed`, "error");
    await loadD3DMetalStatus();
    return null;
  }
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
    const failed = ["asset_missing", "failed", "install_failed"].includes(result.repair.status);
    toast.show(failed ? result.repair.detail : `${component}: ${result.repair.status}`, failed ? "error" : "success");
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
    if (status === "already_installed") {
      toast.show(`${component}: ready`, "success");
      await runRuntimeDoctor();
      runtimeLoading.value = false;
      return;
    }
    if (["asset_missing", "failed", "install_failed"].includes(status)) {
      toast.show(poll.repair.detail || `${component}: ${status}`, "error");
      await runRuntimeDoctor();
      runtimeLoading.value = false;
      return;
    }
  }
  toast.show(`${component}: repair is taking longer than expected — check back`);
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
  if (bottlePreferredMode.value === "d3dmetal") {
    const gameDir = runtimeReport.value?.game_install_path;
    if (!gameDir) {
      toast.show("D3DMetal save requires a detected game install path", "error");
      bottleSaving.value = false;
      return;
    }
    const d3dmetalResult = await api<D3DMetalGptkResponse>("POST", "/d3dmetal/bottles/save", {
      appid: props.game.appid,
      bottleId,
      name: bottleName.value || props.game.name,
      gameDir,
    }, 10 * 60 * 1000);
    bottleSaving.value = false;
    if (d3dmetalResult?.ok && d3dmetalResult.state) {
      d3dmetalState.value = d3dmetalResult.state;
      d3dmetalActions.value = d3dmetalResult.actions ?? [];
      selectedLaunchMode.value = "d3dmetal";
      runtimeReport.value = runtimeReportFromD3DMetalState(d3dmetalState.value, d3dmetalActions.value);
      localStorage.setItem(launchModeStorageKey.value, "d3dmetal");
      pipelineName.value = "D3DMetal";
      pipelineResolvedLocally.value = true;
      toast.show("D3DMetal bottle saved; seed VC runtime DLLs and seed prefix when ready", "success");
      return;
    }
    toast.show(d3dmetalResult?.error ?? "D3DMetal bottle save failed", "error");
    await loadD3DMetalStatus();
    return;
  }

  if (bottlePreferredMode.value !== "d3dmetal") {
    clearD3DMetalPanelState();
    if (reportIsD3DMetal(runtimeReport.value)) {
      runtimeReport.value = {
        ...runtimeReport.value,
        preferred_pipeline: bottlePreferredMode.value,
        pipeline: bottlePreferredMode.value,
        runtime_profile: bottlePreferredMode.value,
        components: [],
        actions: [],
      };
    }
  }

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
        :src="displayedArtworkUrl"
        :alt="game.name"
        class="game-card-cover"
        :class="{ fallback: usingFallbackArtwork }"
        loading="lazy"
        @error="onArtworkError"
      />
      <button v-if="running" class="running-close-button" title="Close game" @click="emit('stop')">
        <IconX width="14" height="14" />
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
              <IconFlaskConical v-if="!runtimeLoading" width="17" height="17" />
              <span v-else class="spinner"></span>
            </button>
            <button class="btn btn-play" @click="playSelectedLaunchMode">Play</button>
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
              <IconShieldPlus
                v-if="!doctorLoading"
                width="16"
                height="16"
              />
              <span v-else class="spinner"></span>
            </button>
            <button
              v-if="developerMode"
              class="icon-button doctor-button"
              :disabled="runtimeLoading"
              title="Run Runtime Doctor"
              @click="runRuntimeDoctor"
            >
              <IconBox
                v-if="!runtimeLoading"
                width="16"
                height="16"
              />
              <span v-else class="spinner"></span>
            </button>
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
                <span>{{ runtimeReport.bottle_id ?? "steam prefix" }} / {{ runtimeProfileLabel(runtimeReport.runtime_profile) }}</span>
              </div>
              <div class="bottle-edit-row">
                <span>Bottle Name</span>
                <div class="bottle-input-row">
                  <input v-model="bottleName" class="bottle-name-input" type="text" :placeholder="game.name" />
                  <button class="icon-button compact-button" title="Use Steam name" @click="resetBottleName">
                    <IconRotateCcw width="14" height="14" />
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
              <div v-if="isD3DMetalBottleSelected() && d3dmetalState" class="doctor-notes d3dmetal-actions">
                <strong>D3DMetal GPTK</strong>
                <div class="runtime-action-row">
                  <span>Homebrew GPTK: {{ d3dmetalState.gptk_homebrew }} / Homebrew payload: {{ d3dmetalState.gptk_payload }}</span>
                </div>
                <div class="runtime-action-row">
                  <span>VC runtimes: {{ d3dmetalState.x64_redist }} / Seed: {{ d3dmetalState.seed }}</span>
                </div>
                <div v-if="d3dmetalState.last_error" class="doctor-notes blocked">{{ d3dmetalState.last_error }}</div>
                <div v-for="action in visibleD3DMetalActions" :key="action.id" class="runtime-action-row">
                  <span>{{ action.detail }}</span>
                  <span v-if="d3dmetalActionReady(action)" class="d3dmetal-ready-state">
                    <span class="doctor-check-state">OK</span>
                    <span>Ready</span>
                  </span>
                  <button
                    v-else
                    class="btn btn-secondary btn-sm"
                    :disabled="d3dmetalLoading || !action.enabled"
                    @click="runD3DMetalPanelAction(action)"
                  >
                    {{ d3dmetalLoading ? "Working..." : action.label }}
                  </button>
                </div>
              </div>
              <div class="doctor-checks">
                <div v-for="component in runtimeReport.components" :key="component.id" class="doctor-check" :class="componentStateClass(component.state)">
                  <span class="doctor-check-state">{{ componentStateIcon(component.state) }}</span>
                  <span class="doctor-check-label">{{ componentLabel(component.id) }}</span>
                  <span class="doctor-check-detail">{{ component.state }}</span>
                </div>
              </div>
              <div v-if="runtimeReport.runtime_assets.length" class="doctor-notes">
                {{ runtimeReport.runtime_assets.length }} runtime assets detected near this install.
              </div>
              <div v-if="runtimeReport.actions.length && !isD3DMetalBottleSelected()" class="doctor-notes blocked">
                <div v-for="action in runtimeReport.actions" :key="action.id" class="runtime-action-row">
                  <span>{{ componentLabel(action.id) }}: {{ action.detail }}</span>
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
  align-self: start;
  min-width: 0;
  width: 100%;
  height: fit-content;
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
:global(:root[data-theme="developer"] .game-card) {
  border-color: rgba(0, 245, 255, 0.22);
  border-radius: 20px;
  box-shadow:
    inset 0 0 0 1px rgba(255, 255, 255, 0.04),
    0 0 0 1px rgba(185, 255, 77, 0.14),
    0 0 34px rgba(255, 46, 247, 0.14),
    0 22px 56px rgba(0, 0, 0, 0.36);
}
.game-card.installed {
  box-shadow:
    0 0 0 1px color-mix(in srgb, var(--card-installed-glow-color) 35%, transparent),
    0 0 40px color-mix(in srgb, var(--card-installed-glow-color) 48%, transparent),
    0 18px 40px color-mix(in srgb, var(--bg-deep) 36%, transparent);
}
:global(:root[data-theme="developer"] .game-card.installed) {
  border-color: rgba(185, 255, 77, 0.34);
  box-shadow:
    inset 0 0 0 1px rgba(255, 255, 255, 0.04),
    0 0 0 1px rgba(185, 255, 77, 0.24),
    0 0 42px rgba(185, 255, 77, 0.20),
    0 24px 58px rgba(0, 0, 0, 0.38);
}
.game-card.uninstalled {
  box-shadow:
    0 0 0 1px color-mix(in srgb, var(--accent) 18%, transparent),
    0 0 24px color-mix(in srgb, var(--accent) 14%, transparent),
    0 14px 34px color-mix(in srgb, var(--bg-deep) 30%, transparent);
}
:global(:root[data-theme="developer"] .game-card.uninstalled) {
  opacity: 0.92;
  filter: saturate(0.9);
}
.game-card:hover {
  border-color: var(--border-strong);
  box-shadow:
    0 0 0 1px color-mix(in srgb, var(--accent) 32%, transparent),
    0 0 36px color-mix(in srgb, var(--accent) 26%, transparent),
    0 22px 48px color-mix(in srgb, var(--bg-deep) 42%, transparent);
  transform: translateY(-1px);
}
:global(:root[data-theme="developer"] .game-card:hover) {
  border-color: rgba(185, 255, 77, 0.58);
  box-shadow:
    inset 0 0 0 1px rgba(0, 245, 255, 0.14),
    0 0 0 1px rgba(185, 255, 77, 0.28),
    0 0 46px rgba(0, 245, 255, 0.20),
    0 26px 64px rgba(0, 0, 0, 0.44);
  transform: translateY(-3px);
}
.game-card.running {
  border-color: var(--success);
  box-shadow:
    0 0 0 1px color-mix(in srgb, var(--success) 48%, transparent),
    0 0 38px color-mix(in srgb, var(--success) 30%, transparent),
    0 22px 48px color-mix(in srgb, var(--bg-deep) 42%, transparent);
}
:global(:root[data-theme="developer"] .game-card.running) {
  border-color: var(--success);
  box-shadow:
    0 0 0 1px rgba(112, 255, 140, 0.38),
    0 0 46px rgba(112, 255, 140, 0.24),
    0 0 80px rgba(0, 245, 255, 0.10),
    0 26px 64px rgba(0, 0, 0, 0.42);
}

.game-card-banner {
  width: 100%;
  aspect-ratio: 16 / 6.25;
  min-height: 116px;
  height: auto;
  background: var(--bg-surface);
  display: flex;
  align-items: center;
  justify-content: center;
  overflow: hidden;
  position: relative;
}
:global(:root[data-theme="developer"] .game-card-banner) {
  border-radius: 18px 18px 0 0;
  background:
    linear-gradient(135deg, rgba(185, 255, 77, 0.15), rgba(255, 46, 247, 0.13)),
    var(--bg-surface);
  box-shadow: inset 0 0 0 1px rgba(0, 245, 255, 0.18);
}
.game-card-cover {
  width: 100%;
  height: 100%;
  object-fit: cover;
  transition:
    transform 0.3s ease,
    filter var(--transition);
}
.game-card-cover.fallback {
  object-fit: contain;
  padding: 26px;
  background:
    radial-gradient(circle at 50% 45%, color-mix(in srgb, var(--accent) 18%, transparent), transparent 48%),
    var(--bg-surface);
}
:global(:root[data-theme="developer"] .game-card-cover) {
  filter: saturate(1.18) contrast(1.03);
}
:global(:root[data-theme="developer"] .game-card-cover.fallback) {
  background:
    radial-gradient(circle at 26% 32%, rgba(255, 46, 247, 0.24), transparent 36%),
    radial-gradient(circle at 76% 64%, rgba(0, 245, 255, 0.20), transparent 40%),
    linear-gradient(135deg, rgba(185, 255, 77, 0.13), rgba(9, 7, 15, 0.92));
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
.game-card-body {
  display: flex;
  flex-direction: column;
  flex: 1 1 auto;
  background: var(--game-card-body-bg, transparent);
  color: var(--game-card-text, var(--text-primary));
  padding: 14px;
}
:global(:root[data-theme="developer"] .game-card-body) {
  padding: 14px;
}
.game-card-heading {
  display: grid;
  grid-template-columns: minmax(0, 1fr) minmax(0, auto);
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
:global(:root[data-theme="developer"] .game-card-title) {
  color: #f8ffe7;
  font-family: var(--font-mono);
}
.game-card-meta {
  display: flex;
  align-items: center;
  gap: 8px;
  min-height: 24px;
  margin-bottom: 12px;
  flex-wrap: wrap;
  min-width: 0;
}
.game-card-size {
  min-width: 0;
  font-size: 11px;
  color: var(--game-card-dim, var(--text-dim));
}
.route-chip {
  display: inline-flex;
  align-items: center;
  min-width: 0;
  min-height: 22px;
  padding: 2px 8px;
  border-radius: var(--radius-sm);
  background: var(--success-bg);
  color: var(--success);
  font-size: 11px;
  font-weight: 700;
}
:global(:root[data-theme="developer"] .route-chip),
:global(:root[data-theme="developer"] .tool-chip),
:global(:root[data-theme="developer"] .launch-mode-select),
:global(:root[data-theme="developer"] .icon-button),
:global(:root[data-theme="developer"] .danger-link) {
  border-radius: 999px;
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
  min-width: 0;
  flex: 1 1 auto;
}
.wide-action {
  min-width: 0;
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
  min-width: 34px;
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

@media (max-width: 760px) {
  :global(:root[data-theme="developer"] .game-card-banner) {
    min-height: 102px;
  }
  :global(:root[data-theme="developer"] .game-card-body) {
    padding: 12px;
  }
  .primary-action-row,
  .secondary-action-grid {
    gap: 8px;
  }
  .game-card-body {
    padding: 12px;
  }
  .game-card-title {
    font-size: 13px;
  }
  .route-chip,
  .bottle-chip,
  .steam-emu-toggle,
  .game-card-size {
    font-size: 10px;
  }
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
.doctor-check.check-missing .doctor-check-state {
  color: var(--error);
}
.doctor-check.check-warn .doctor-check-state {
  color: var(--warning, #facc15);
}
.doctor-check.check-unknown .doctor-check-state {
  color: var(--text-dim);
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
.d3dmetal-ready-state {
  display: inline-flex;
  align-items: center;
  gap: 4px;
  color: var(--success);
  font-weight: 700;
  white-space: nowrap;
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
