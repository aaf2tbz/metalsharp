<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, ref } from "vue";
import { getAPI } from "../composables/useApi";

const sample = ref<ProcessManagerSample | null>(null);
const loading = ref(true);
const metalfxOn = ref(false);
const gpuAccelOn = ref(false);
const actionMessage = ref("Cmd+P toggles this overlay mid-session");
let pollTimer: ReturnType<typeof setInterval> | null = null;

const api = getAPI();

const ramPercent = computed(() => {
  const s = sample.value;
  if (!s?.ram_total_bytes) return 0;
  return Math.max(0, Math.min(100, (s.ram_used_bytes / s.ram_total_bytes) * 100));
});

const cpuPercent = computed(() => Math.max(0, Math.min(100, sample.value?.cpu_percent ?? 0)));
const gpuPercent = computed(() => Math.max(0, Math.min(100, sample.value?.gpu_percent ?? 0)));
const fpsLabel = computed(() => (sample.value?.fps == null ? "HOOK" : Math.round(sample.value.fps).toString()));
const tempLabel = computed(() => (sample.value?.cpu_temp_c == null ? "SENSOR" : `${Math.round(sample.value.cpu_temp_c)}°C`));
const ramLabel = computed(() => {
  const s = sample.value;
  if (!s?.ram_total_bytes) return "--";
  return `${formatBytes(s.ram_used_bytes)} / ${formatBytes(s.ram_total_bytes)}`;
});
const coresLabel = computed(() => {
  const s = sample.value;
  if (!s) return "--";
  return `${s.cores_used.toFixed(1)} / ${s.cores_total}`;
});
const processes = computed(() => sample.value?.processes?.slice(0, 10) ?? []);

function formatBytes(bytes: number): string {
  if (!Number.isFinite(bytes) || bytes <= 0) return "--";
  const units = ["B", "KB", "MB", "GB", "TB"];
  let value = bytes;
  let i = 0;
  while (value >= 1024 && i < units.length - 1) {
    value /= 1024;
    i += 1;
  }
  return `${value.toFixed(value >= 10 ? 1 : 2)} ${units[i]}`;
}

async function refresh() {
  try {
    sample.value = await api.processManagerSample();
  } catch (error) {
    console.error("process manager sample failed", error);
  } finally {
    loading.value = false;
  }
}

async function closeOverlay() {
  await api.processManagerClose();
}

async function runAction(action: "metalfx" | "gpu-acceleration" | "quit-game" | "view-steam") {
  if (action === "metalfx") {
    metalfxOn.value = !metalfxOn.value;
    actionMessage.value = `MetalFX visual surface ${metalfxOn.value ? "armed" : "parked"}; runtime hook pending`;
  } else if (action === "gpu-acceleration") {
    gpuAccelOn.value = !gpuAccelOn.value;
    actionMessage.value = `GPU acceleration visual surface ${gpuAccelOn.value ? "armed" : "parked"}; runtime hook pending`;
  } else if (action === "quit-game") {
    actionMessage.value = "Force-killing active Wine/Steam game session...";
  } else {
    actionMessage.value = "Bringing Steam forward...";
  }

  const result = await api.processManagerAction(action);
  if (!result?.ok) {
    actionMessage.value = result?.error ?? "Action failed";
  } else if (action === "quit-game") {
    actionMessage.value = "Game/session kill signal sent";
    await refresh();
  } else if (action === "view-steam") {
    actionMessage.value = "Steam focus requested";
  }
}

function onKeyDown(event: KeyboardEvent) {
  if (event.key === "Escape") void closeOverlay();
}

onMounted(() => {
  document.documentElement.dataset.theme = "developer";
  document.body.classList.add("process-manager-overlay-body");
  window.addEventListener("keydown", onKeyDown);
  void refresh();
  pollTimer = setInterval(refresh, 1500);
});

onBeforeUnmount(() => {
  if (pollTimer) clearInterval(pollTimer);
  window.removeEventListener("keydown", onKeyDown);
  document.body.classList.remove("process-manager-overlay-body");
});
</script>

<template>
  <main class="pm-shell" aria-label="MetalSharp Process Manager">
    <section class="pm-panel">
      <header class="pm-header">
        <div class="pm-brand">
          <div class="pm-logo-wrap"><img src="../icon.png" alt="MetalSharp" /></div>
          <div>
            <p class="pm-kicker">LIVE SESSION HUD</p>
            <h1>MetalSharp Process Manager</h1>
          </div>
        </div>
        <button class="pm-close" type="button" title="Close" @click="closeOverlay">×</button>
      </header>

      <div class="pm-grid">
        <article class="pm-stat pm-stat-fps">
          <span class="pm-label">Active FPS</span>
          <strong>{{ fpsLabel }}</strong>
          <small>{{ sample?.fps == null ? "render hook pending" : "frames/sec" }}</small>
        </article>
        <article class="pm-stat">
          <span class="pm-label">CPU Temp</span>
          <strong>{{ tempLabel }}</strong>
          <small>{{ sample?.cpu_temp_c == null ? "private sensor unavailable" : sample?.cpu_temp_source ?? "private PMU sensor" }}</small>
        </article>
        <article class="pm-stat">
          <span class="pm-label">Cores Used</span>
          <strong>{{ coresLabel }}</strong>
          <small>{{ cpuPercent.toFixed(1) }}% CPU</small>
          <div class="pm-meter"><span :style="{ width: cpuPercent + '%' }"></span></div>
        </article>
        <article class="pm-stat">
          <span class="pm-label">RAM Usage</span>
          <strong>{{ ramPercent.toFixed(0) }}%</strong>
          <small>{{ ramLabel }}</small>
          <div class="pm-meter"><span :style="{ width: ramPercent + '%' }"></span></div>
        </article>
        <article class="pm-stat pm-stat-wide">
          <span class="pm-label">GPU Usage</span>
          <strong>{{ sample?.gpu_percent == null ? "SYS" : gpuPercent.toFixed(0) + "%" }}</strong>
          <small>{{ sample?.gpu_label ?? "system GPU telemetry unavailable" }}</small>
          <div class="pm-meter pm-meter-magenta"><span :style="{ width: gpuPercent + '%' }"></span></div>
        </article>
      </div>

      <section class="pm-action-grid" aria-label="Process manager actions">
        <button class="pm-action" :class="{ armed: metalfxOn }" type="button" @click="runAction('metalfx')">
          <span>MetalFX</span>
          <strong>Planned</strong>
          <small>upscaling control coming soon</small>
        </button>
        <button class="pm-action danger" type="button" @click="runAction('quit-game')">
          <span>Quit Game</span>
          <strong>Force Kill</strong>
          <small>active Wine/Steam session</small>
        </button>
        <button class="pm-action" :class="{ armed: gpuAccelOn }" type="button" @click="runAction('gpu-acceleration')">
          <span>GPU Acceleration</span>
          <strong>Planned</strong>
          <small>runtime control coming soon</small>
        </button>
        <button class="pm-action cyan" type="button" @click="runAction('view-steam')">
          <span>View Steam</span>
          <strong>Focus</strong>
          <small>bring client forward</small>
        </button>
      </section>

      <section class="pm-processes">
        <div class="pm-process-header">
          <div>
            <p class="pm-kicker">PROCESS VIEW</p>
            <h2>Wine / Steam Session</h2>
          </div>
          <span>{{ loading ? "sampling" : processes.length + " rows" }}</span>
        </div>
        <div class="pm-process-list">
          <div v-if="processes.length === 0" class="pm-empty">No Wine/Steam/MetalSharp session processes detected yet.</div>
          <div v-for="proc in processes" :key="proc.pid" class="pm-process-row">
            <div>
              <strong>{{ proc.name.split('/').pop() }}</strong>
              <small>pid {{ proc.pid }} · {{ proc.command }}</small>
            </div>
            <span>{{ proc.cpu_percent.toFixed(1) }}% CPU</span>
            <span>{{ proc.mem_percent.toFixed(1) }}% MEM</span>
          </div>
        </div>
      </section>

      <footer class="pm-footer">
        <span>{{ actionMessage }}</span>
        <span>{{ sample?.source ?? "waiting" }}</span>
      </footer>
    </section>
  </main>
</template>

<style scoped>
.pm-shell {
  width: 100vw;
  height: 100vh;
  display: flex;
  align-items: center;
  justify-content: center;
  padding: 12px;
  color: var(--text-primary);
  background:
    radial-gradient(circle at 18% 0%, rgba(255, 46, 247, 0.22), transparent 36%),
    radial-gradient(circle at 88% 14%, rgba(0, 245, 255, 0.18), transparent 34%),
    radial-gradient(circle at 50% 100%, rgba(185, 255, 77, 0.13), transparent 42%);
}
.pm-panel {
  width: min(720px, calc(100vw - 18px));
  max-height: calc(100vh - 18px);
  overflow: auto;
  border: 1px solid var(--border-strong);
  border-radius: 16px;
  background:
    linear-gradient(140deg, rgba(9, 7, 15, 0.82), rgba(19, 16, 34, 0.74) 46%, rgba(5, 35, 44, 0.64)),
    rgba(9, 7, 15, 0.74);
  box-shadow:
    0 0 0 1px rgba(255, 255, 255, 0.05) inset,
    0 0 42px rgba(185, 255, 77, 0.12),
    0 30px 90px rgba(0, 0, 0, 0.58);
  backdrop-filter: blur(26px) saturate(170%);
  -webkit-backdrop-filter: blur(26px) saturate(170%);
}
.pm-header {
  -webkit-app-region: drag;
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 20px;
  padding: 14px 18px 9px;
}
.pm-brand {
  display: flex;
  align-items: center;
  gap: 11px;
}
.pm-logo-wrap {
  width: 44px;
  height: 44px;
  display: grid;
  place-items: center;
  border-radius: 14px;
  border: 1px solid rgba(185, 255, 77, 0.38);
  background:
    linear-gradient(135deg, rgba(185, 255, 77, 0.16), rgba(0, 245, 255, 0.12), rgba(255, 46, 247, 0.14)),
    rgba(13, 11, 23, 0.82);
  box-shadow: 0 0 24px rgba(185, 255, 77, 0.16);
}
.pm-logo-wrap img {
  width: 32px;
  height: 32px;
  object-fit: contain;
}
.pm-kicker {
  margin: 0 0 4px;
  font-family: var(--font-mono);
  font-size: 9px;
  letter-spacing: 0.2em;
  color: var(--accent-hover);
  text-transform: uppercase;
}
h1,
h2 {
  margin: 0;
}
h1 {
  font-family: var(--font-logo);
  font-size: 13px;
  line-height: 1.45;
  color: var(--accent);
  text-shadow: 0 0 22px rgba(185, 255, 77, 0.24);
}
h2 {
  font-size: 14px;
  color: var(--text-bright);
}
.pm-close {
  -webkit-app-region: no-drag;
  width: 32px;
  height: 32px;
  border: 1px solid rgba(255, 79, 119, 0.44);
  border-radius: 999px;
  background: rgba(255, 79, 119, 0.1);
  color: #ffd7df;
  font-size: 20px;
  line-height: 1;
  cursor: pointer;
}
.pm-grid {
  display: grid;
  grid-template-columns: repeat(4, minmax(0, 1fr));
  gap: 9px;
  padding: 6px 18px 10px;
}
.pm-stat {
  min-height: 84px;
  padding: 10px 12px;
  border: 1px solid var(--border);
  border-radius: 16px;
  background: linear-gradient(180deg, rgba(255, 255, 255, 0.055), transparent), rgba(13, 11, 23, 0.62);
  box-shadow: inset 0 0 0 1px rgba(255, 255, 255, 0.035);
}
.pm-stat-wide {
  grid-column: span 4;
  min-height: 64px;
}
.pm-label,
.pm-stat small {
  display: block;
  color: var(--text-secondary);
  font-size: 10px;
}
.pm-stat strong {
  display: block;
  margin: 5px 0 3px;
  font-family: var(--font-mono);
  font-size: 20px;
  color: var(--text-bright);
}
.pm-stat-fps strong {
  color: var(--accent);
}
.pm-meter {
  height: 5px;
  margin-top: 7px;
  overflow: hidden;
  border-radius: 999px;
  background: rgba(0, 245, 255, 0.11);
}
.pm-meter span {
  display: block;
  height: 100%;
  border-radius: inherit;
  background: linear-gradient(90deg, var(--accent), var(--accent-hover));
}
.pm-meter-magenta span {
  background: linear-gradient(90deg, var(--accent-dim), var(--accent-hover));
}
.pm-action-grid {
  display: grid;
  grid-template-columns: repeat(4, minmax(0, 1fr));
  gap: 9px;
  padding: 0 18px 10px;
}
.pm-action {
  min-height: 74px;
  border: 1px solid rgba(0, 245, 255, 0.24);
  border-radius: 16px;
  background:
    linear-gradient(135deg, rgba(0, 245, 255, 0.12), rgba(255, 46, 247, 0.08)),
    rgba(13, 11, 23, 0.74);
  color: var(--text-primary);
  text-align: left;
  padding: 10px 12px;
  cursor: pointer;
  transition: transform var(--transition), border-color var(--transition), box-shadow var(--transition);
}
.pm-action:hover,
.pm-action.armed {
  transform: translateY(-2px);
  border-color: var(--accent);
  box-shadow: 0 0 26px rgba(185, 255, 77, 0.16);
}
.pm-action span,
.pm-action small {
  display: block;
  color: var(--text-secondary);
  font-size: 10px;
}
.pm-action strong {
  display: block;
  margin: 5px 0;
  font-size: 14px;
  color: var(--accent);
}
.pm-action.danger {
  border-color: rgba(255, 79, 119, 0.38);
}
.pm-action.danger strong {
  color: var(--error);
}
.pm-action.cyan strong {
  color: var(--accent-hover);
}
.pm-processes {
  margin: 0 18px 10px;
  border: 1px solid var(--border);
  border-radius: 16px;
  background: rgba(9, 7, 15, 0.46);
  overflow: hidden;
}
.pm-process-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 10px 12px;
  border-bottom: 1px solid var(--border);
}
.pm-process-header > span,
.pm-footer {
  color: var(--text-dim);
  font-family: var(--font-mono);
  font-size: 10px;
}
.pm-process-list {
  max-height: 96px;
  overflow: auto;
}
.pm-process-row,
.pm-empty {
  display: grid;
  grid-template-columns: minmax(0, 1fr) 74px 74px;
  gap: 8px;
  align-items: center;
  padding: 7px 10px;
  border-bottom: 1px solid rgba(0, 245, 255, 0.08);
  font-family: var(--font-mono);
  font-size: 10px;
}
.pm-process-row strong,
.pm-process-row small {
  display: block;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.pm-process-row strong {
  color: var(--text-primary);
}
.pm-process-row small,
.pm-empty {
  color: var(--text-dim);
}
.pm-empty {
  display: block;
}
.pm-footer {
  display: flex;
  justify-content: space-between;
  gap: 12px;
  padding: 0 18px 12px;
}
@media (max-width: 760px) {
  .pm-grid,
  .pm-action-grid {
    grid-template-columns: repeat(2, minmax(0, 1fr));
  }
  .pm-stat-wide {
    grid-column: span 2;
  }
  h1 {
    font-size: 13px;
  }
}
</style>
