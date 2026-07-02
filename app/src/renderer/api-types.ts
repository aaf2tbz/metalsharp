interface Game {
  id: string;
  name: string;
  exe_path: string;
  platform: string;
  steam_app_id?: number;
  size_bytes?: number;
  metalsharp_compatible: boolean;
  cover_art?: string;
}

interface SteamStatus {
  installed: boolean;
  path?: string;
  login_state?: LoginState;
  mac_installed?: boolean;
  mac_running?: boolean;
  mac_path?: string;
  mac_install_url?: string;
  running: boolean;
}

interface LoginState {
  state: "logged_in" | "logged_out" | "unknown";
  account?: { name: string; remembered: boolean }[] | null;
}

interface AppConfig {
  ok: boolean;
  launch_mode?: "native" | "wine";
  wine_available?: boolean;
  native_available: boolean;
  mono_available?: boolean;
  graphicsRuntimeLogs?: boolean;
  graphics_runtime_logs?: boolean;
}

interface UpdateStatus {
  ok: boolean;
  available: boolean;
  current_version: string;
  latest_version: string;
  download_url: string;
  download_size: number;
  release_notes: string;
  release_name: string;
  updates_disabled?: boolean;
  error?: string;
}

interface UpdateProgress {
  status: string;
  percent: number;
  message: string;
  error: string | null;
}

interface InstallStatus {
  phase: string;
  percent: number;
  message: string;
  error: string | null;
  new_version: string | null;
  dmg_path?: string | null;
  timestamp: number;
}

interface UpdaterReadyResult {
  ok: boolean;
  error?: string;
  scriptPath?: string;
  candidates?: string[];
}

interface CrashReportSummary {
  id: string;
  timestamp: string;
  game: string;
  exit_code: number;
}

interface SharpApp {
  id: string;
  name: string;
  exe_path: string;
  install_dir: string;
  cover: string | null;
  cover_position_x: number;
  cover_position_y: number;
  engine: string;
  launch_args: string[];
  user_launch_args: string[];
  bottle_id?: string | null;
  installed_at: string;
  size_bytes: number;
}

interface BackendResponse {
  ok: boolean;
  data?: unknown;
  error?: string;
  pid?: number;
  games?: unknown[];
  drives?: unknown[];
  prefixes?: unknown[];
  mapped?: number;
}

interface SetupState {
  ok: boolean;
  completed: boolean;
  savedCompleted?: boolean;
  step: number;
  deviceName: string;
  steamApiKeySet: boolean;
  runtimeMigrationRequired?: boolean;
  dxmtRuntime?: {
    current: boolean;
    filesReady: boolean;
    installedVersion?: string | null;
    requiredVersion: string;
    manifestPath: string;
  };
}

interface Dependency {
  id: string;
  name: string;
  desc: string;
  installed: boolean;
  required: boolean;
  installCmd: string;
}

interface DependenciesResponse {
  ok: boolean;
  allInstalled: boolean;
  dependencies: Dependency[];
}

type RuntimeLaneStatus = "available" | "external" | "planned";

interface RuntimeLaneContract {
  id: string;
  name: string;
  family: string;
  status: RuntimeLaneStatus;
  source_scopes: string[];
  requires_wine: boolean;
  supports_win32: boolean;
  supports_win64: boolean;
  host_arch: string;
  windows_arch: string;
  graphics_apis: string[];
  prefix_policy: string;
  runtime_surfaces: string[];
  runtime_surface_paths: string[];
  required_pe_dlls: string[];
  required_unix_sidecars: string[];
  dyld_paths: string[];
  winedllpath_dirs: string[];
  shader_cache_lane?: string | null;
  fallback_lanes: string[];
  doctor_checks: string[];
  repair_actions: string[];
  notes: string;
}

interface RuntimeContractsResponse {
  ok: boolean;
  schema: "metalsharp.runtime.contracts.v1";
  canonicalM12Surface: "dxmt_m12";
  canonicalM12InstalledPath: "runtime/wine/lib/dxmt_m12";
  surfaces: { id: string; installedPath?: string | null }[];
  contracts: RuntimeLaneContract[];
}

interface RuntimeManifestResponse {
  ok: boolean;
  schema: "metalsharp.runtime.manifest.v1";
  manifestPath: string;
  expected: Record<string, unknown>;
  persisted: Record<string, unknown>;
  validation: { ok?: boolean; checks?: unknown[] };
  artifacts: Record<string, unknown>;
}

interface NativeMonoLaneDoctor {
  id: "native_mono_arm64" | "native_mono_x86";
  runtime_root: string;
  mono_binary: string;
  expected_arch: "arm64" | "x86_64";
  present: boolean;
  detected_architectures: string[];
  architecture_ok: boolean;
  ready: boolean;
  blockers: string[];
}

interface NativeMonoSupportInventoryEntry {
  id: string;
  label: string;
  path: string;
  present: boolean;
  required: boolean;
  sha256?: string | null;
}

interface NativeMonoPlatformDoctor {
  schema_version: 1;
  ok: boolean;
  read_only: true;
  metalsharp_home: string;
  lanes: NativeMonoLaneDoctor[];
  support_inventory: NativeMonoSupportInventoryEntry[];
  game?: unknown | null;
  next_actions: string[];
}

type LaunchValidationStatus = "proven" | "filesystem_validated" | "pending_launch_proof" | "policy_blocked";

interface LaunchValidationEntry {
  schema: "metalsharp.launch.validation.entry.v1";
  id: string;
  source: string;
  route: string;
  runtimeContractId: string;
  status: LaunchValidationStatus;
  requiredEvidence: string[];
  evidence: Record<string, unknown>;
  limitations: string[];
  nextActions: string[];
}

interface LaunchValidationResponse {
  ok: boolean;
  schema: "metalsharp.launch.validation.matrix.v1";
  readOnly: true;
  summary: {
    total: number;
    proven: number;
    pending: number;
    filesystemValidated: number;
    policyBlocked: number;
  };
  entries: LaunchValidationEntry[];
  invariants: string[];
}

interface SourceAdapter {
  id: "steam" | "gog" | "sharp" | string;
  schema: "metalsharp.source.adapter.v1";
  displayName: string;
  kind: "storefront" | "local_library" | string;
  libraryEndpoint: string;
  statusEndpoint: string;
  launchEndpoint: string;
  prepareEndpoint?: string | null;
  runtimeContractIds: string[];
  prefixPolicy: {
    id: string;
    type: string;
    path?: string | null;
    mustNotAlias: string[];
  };
  ready: boolean;
  status: string;
  installed: boolean | number | null;
  running: boolean | null;
  details: Record<string, unknown>;
  capabilities: Record<string, unknown>;
  limitations: string[];
}

interface SourceAdaptersResponse {
  ok: boolean;
  schema: "metalsharp.source.adapters.v1";
  adapters: SourceAdapter[];
}

interface SourcePreparePreviewResponse {
  ok: boolean;
  schema: "metalsharp.source.prepare.preview.v1";
  readOnly: true;
  source: string;
  appId: string;
  route: string;
  launchReceiptPreview: Record<string, unknown> & { schema: "metalsharp.launch.receipt.v1"; preview: true; dryRun: true };
  mutates: false;
  launches: false;
  next: string;
}

interface ReceiptInventoryBucket {
  id: string;
  label: string;
  root: string;
  pattern: string;
  present: boolean;
  count: number;
  latest?: {
    path: string;
    schema?: string | null;
    runtimeContractId?: string | null;
    source?: string | null;
  } | null;
  paths: string[];
}

interface ReceiptInventoryResponse {
  ok: boolean;
  schema: "metalsharp.receipts.inventory.v1";
  readOnly: true;
  metalsharpHome: string;
  total: number;
  buckets: ReceiptInventoryBucket[];
  invariants: string[];
}

interface RuntimeDiagnosticsResponse {
  ok: boolean;
  schema: "metalsharp.runtime.diagnostics.v1";
  readOnly: true;
  summary: string;
  paths: Record<string, string>;
  contracts: {
    schema: "metalsharp.runtime.contracts.v1";
    canonicalM12Surface: "dxmt_m12";
    canonicalM12InstalledPath: "runtime/wine/lib/dxmt_m12";
    canonicalM12Ok: boolean;
    total: number;
    available: string[];
    planned: string[];
    external: string[];
  };
  runtime: {
    ready: boolean;
    wineBinaryPresent: boolean;
    dxmtCurrent: boolean;
    dxmtM12Current: boolean;
    manifestOk: boolean;
    manifest: RuntimeManifestResponse;
  };
  prefixes: Record<string, unknown>;
  vulkan: {
    ok: boolean;
    readOnly: true;
    runtimeLibraryRoot: string;
    dxvk: RuntimeVulkanSurfaceReport;
    vkd3d: RuntimeVulkanSurfaceReport;
    icd: {
      ok: boolean;
      icdDir: string;
      moltenvkRuntimePath: string;
      moltenvkRuntimePresent: boolean;
      present: boolean;
      allPointToRuntimeMoltenVK: boolean;
      entries: { path: string; libraryPath?: string | null; pointsToRuntimeMoltenVK: boolean }[];
    };
    dxvkStateCache: {
      ok: boolean;
      readOnly: true;
      cacheRoot: string;
      method: "permission_bits_no_probe_file";
      entries: { lane: string; path: string; exists: boolean; writableByMode: boolean; checkedPath?: string | null; reason?: string | null }[];
    };
    limitations: { id: string; severity: "info" | "warning" | "error"; detail: string }[];
  };
  lanes: {
    total: number;
    ready: number;
    availableTotal: number;
    availableReady: number;
    planned: number;
    external: number;
    entries: {
      id: string;
      name: string;
      family: string;
      status: RuntimeLaneStatus;
      ready: boolean;
      blockers: string[];
      requiresWine: boolean;
      sourceScopes: string[];
      runtimeSurfacePaths: string[];
      artifactSummary: null | {
        total: number;
        present: number;
        missing: number;
        allPresent: boolean;
      };
    }[];
  };
  nativeMono: NativeMonoPlatformDoctor;
  sources: {
    steam: {
      id: "steam_background";
      prefixPath: string;
      prefixPresent: boolean;
      usesDedicatedPrefix: boolean;
    };
    gog: {
      id: "gogdl_wine";
      ok: boolean;
      gogdlAvailable: boolean;
      gogdlPath?: string | null;
      authPresent: boolean;
      authPath: string;
      configPath: string;
      supportPath: string;
      prefixPath: string;
      prefixPresent: boolean;
      mustNotUsePrefixSteam: boolean;
    };
  };
  updateGuard: {
    ok: boolean;
    releaseFeed: "disabled" | "custom" | "public";
    publicUpdatesDisabled: boolean;
    customRepoConfigured: boolean;
    allowPublicUpdates: boolean;
    usingPublicRepo: boolean;
    effectiveRepoApi?: string | null;
    publicRepoApi: string;
    reason: string;
  };
  installReplacementGuard: { allowedNow: false; reason: string };
  nextActions: string[];
}

interface RuntimeVulkanSurfaceReport {
  ok: boolean;
  root: string;
  present: number;
  total: number;
  missing: number;
  entries: { path: string; relativePath: string; present: boolean }[];
}

type MetalsharpAPI = {
  request: (
    method: string,
    url: string,
    body?: Record<string, unknown>,
    timeoutMs?: number,
  ) => Promise<BackendResponse>;
  isFirstLaunch: () => Promise<boolean>;
  isMigrationMode: () => Promise<boolean>;
  restartAfterMigration: () => Promise<{ ok: boolean; error?: string; deletedDmg?: string | null; launched?: string }>;
  ejectDmg: () => Promise<void>;
  installDeps: (command: string) => Promise<{ ok: boolean; error?: string }>;
  installHomebrew: () => Promise<{ ok: boolean; error?: string }>;
  openInFinder: (path: string) => Promise<void>;
  openLogsFolder: () => Promise<{ ok: boolean; path?: string; error?: string }>;
  openMetalsharpFolder: () => Promise<{ ok: boolean; path?: string; error?: string }>;
  repairDataAccess: () => Promise<{
    ok: boolean;
    path?: string;
    checks?: { dir: string; ok: boolean; error?: string }[];
    error?: string;
  }>;
  copyText: (text: string) => Promise<{ ok: boolean; error?: string }>;
  restartBackend: () => Promise<{ ok: boolean; error?: string }>;
  isBackendAlive: () => Promise<boolean>;
  updaterEnsureReady: () => Promise<UpdaterReadyResult>;
  updaterSpawnInstall: (
    dmgPath: string,
    backendPid: number,
    targetVersion: string,
  ) => Promise<{ ok: boolean; error?: string }>;
  updaterInstallStatus: () => Promise<InstallStatus | null>;
  updaterClearStatus: () => Promise<void>;
  backendGetPid: () => Promise<number | null>;
  migrateCheck: () => Promise<BackendResponse>;
  migrateStart: () => Promise<BackendResponse>;
  migrateProgress: () => Promise<BackendResponse>;
  quitApp: () => void;
  uninstallApp: () => void;
  pickExeFile: () => Promise<string | null>;
  pickImageFile: () => Promise<string | null>;
  pickDirectory: (title?: string) => Promise<string | null>;
  gogOAuthLogin: (authUrl: string) => Promise<{ ok: boolean; code?: string; redirectUrl?: string; error?: string }>;
};
