<script setup lang="ts">
import { ref, onMounted, onUnmounted, computed } from "vue";
import IconCheck from "~icons/lucide/check";
import IconInfo from "~icons/lucide/info";

const status = ref("idle");
const step = ref(0);
const total = ref(0);
const message = ref("Checking migration status...");
const error = ref<string | null>(null);
const complete = ref(false);
const launching = ref(false);
const retrying = ref(false);
const spinnerEpoch = ref(0);
const restoringData = ref(false);

type MigrationStageIndex = 0 | 1 | 2 | 3;
const currentStage = ref<MigrationStageIndex>(0);

let pollTimer: ReturnType<typeof setInterval> | null = null;
let spinnerWatchdog: ReturnType<typeof setInterval> | null = null;
let pollInFlight = false;
let pollFailures = 0;

const percent = computed(() => {
  if (total.value === 0) return 0;
  return Math.min(100, Math.max(0, Math.round((step.value / total.value) * 100)));
});

const stages = computed(() => [
  { name: restoringData.value ? "Restore Data" : "Preserve Data", detail: "Settings, bottles, prefixes" },
  { name: "Install Runtime", detail: "Matched graphics components" },
  { name: "Refresh Bottles", detail: "Update profile-owned DLLs" },
  { name: "Verify", detail: "Runtime and saved routes" },
]);

function stageFromMessage(value: string): MigrationStageIndex | null {
  const detail = value.toLowerCase();

  // Restore intentionally returns to the first card after runtime installation.
  // Check it before bottle/prefix terms because restore messages can contain both.
  if (detail.includes("restor")) return 0;
  if (
    detail.includes("refreshing saved bottle") ||
    detail.includes("refreshing bottle") ||
    detail.includes("bottle refresh")
  )
    return 2;
  if (detail.includes("verif") || detail.includes("migration ready") || detail.includes("update ready")) return 3;
  if (detail.includes("preserv") || detail.includes("backup")) return 0;
  if (detail.includes("install") || detail.includes("runtime") || detail.includes("cleaning stale")) return 1;
  return null;
}

function updateVisualStage(value: string) {
  const detectedStage = stageFromMessage(value);
  if (detectedStage === null) return;
  if (detectedStage === 0 && value.toLowerCase().includes("restor")) restoringData.value = true;
  currentStage.value = detectedStage;
}

function stageCompleted(index: number) {
  if (complete.value) return true;

  if (restoringData.value) {
    if (currentStage.value === 0) return index === 1;
    if (currentStage.value === 1) return index === 0;
  }

  return index < currentStage.value;
}

function stageClasses(index: number) {
  const active = currentStage.value === index && !complete.value && !error.value;
  const completed = !active && stageCompleted(index);
  return { active, completed, pending: !active && !completed };
}

const title = computed(() => {
  if (complete.value) return "Update complete";
  if (error.value) return "Migration needs attention";
  return "Updating MetalSharp";
});

const MAX_START_RETRIES = 20;
const START_RETRY_DELAY_MS = 500;

async function startMigration(retriesLeft = MAX_START_RETRIES) {
  try {
    const res = await window.metalsharp.migrateStart();
    if (res?.ok) {
      message.value = "Migration started...";
      startPolling();
    } else if (res?.error?.includes("migration already in progress")) {
      message.value = "Migration already running...";
      startPolling();
    } else {
      const errorText = res?.error ?? "Failed to start migration";
      if (retriesLeft > 0 && shouldRetryBackendError(errorText)) {
        message.value = "Waiting for backend to start...";
        await new Promise((r) => setTimeout(r, START_RETRY_DELAY_MS));
        await startMigration(retriesLeft - 1);
      } else {
        error.value = errorText;
        message.value = `Error: ${error.value}`;
      }
    }
  } catch (e: unknown) {
    const errorText = e instanceof Error ? e.message : "Network error";
    if (retriesLeft > 0 && shouldRetryBackendError(errorText)) {
      message.value = "Waiting for backend to start...";
      await new Promise((r) => setTimeout(r, START_RETRY_DELAY_MS));
      await startMigration(retriesLeft - 1);
    } else {
      error.value = errorText;
      message.value = `Error: ${error.value}`;
    }
  }
}

function shouldRetryBackendError(errorText: string) {
  return (
    errorText.includes("ECONNREFUSED") ||
    errorText.includes("timeout") ||
    errorText.includes("did not start in time") ||
    errorText.includes("Migration backend unavailable")
  );
}

async function pollProgress() {
  if (pollInFlight || complete.value || !!error.value) return;
  pollInFlight = true;
  try {
    const res = await window.metalsharp.migrateProgress();
    if (!res || res.ok === false) {
      pollFailures += 1;
      message.value = "Reconnecting to the migration backend...";
      if (pollFailures >= 12) {
        error.value = res?.error ?? "Migration backend stopped responding";
        message.value = `Error: ${error.value}`;
        stopPolling();
      }
      return;
    }
    pollFailures = 0;
    const data = res.data ?? res;
    status.value = data.status ?? "idle";
    step.value = data.step ?? 0;
    total.value = data.total ?? 0;
    if (typeof data.message === "string" && data.message.trim()) {
      message.value = data.message;
      updateVisualStage(data.message);
    }
    error.value = typeof data.error === "string" && data.error.trim() ? data.error : null;

    if (status.value === "complete") {
      complete.value = true;
      stopPolling();
    } else if (status.value === "error") {
      error.value ??= "Migration stopped before it could complete.";
      message.value = `Error: ${error.value}`;
      stopPolling();
    }
  } catch (e: unknown) {
    pollFailures += 1;
    if (pollFailures >= 12) {
      error.value = e instanceof Error ? e.message : "Migration backend stopped responding";
      message.value = `Error: ${error.value}`;
      stopPolling();
    }
  } finally {
    pollInFlight = false;
  }
}

function startPolling() {
  if (pollTimer) return;
  void pollProgress();
  pollTimer = setInterval(pollProgress, 500);
}

function stopPolling() {
  if (pollTimer) {
    clearInterval(pollTimer);
    pollTimer = null;
  }
}

async function retryMigration() {
  if (retrying.value) return;
  retrying.value = true;
  stopPolling();
  pollFailures = 0;
  pollInFlight = false;
  error.value = null;
  complete.value = false;
  restoringData.value = false;
  currentStage.value = 0;
  status.value = "idle";
  step.value = 0;
  total.value = 0;
  message.value = "Restarting migration...";
  try {
    await startMigration();
  } finally {
    retrying.value = false;
  }
}

function startSpinnerWatchdog() {
  spinnerWatchdog = setInterval(() => {
    if (!complete.value && !error.value) spinnerEpoch.value += 1;
  }, 4000);
}

function stopSpinnerWatchdog() {
  if (spinnerWatchdog) {
    clearInterval(spinnerWatchdog);
    spinnerWatchdog = null;
  }
}

async function restartApp() {
  if (launching.value) return;
  launching.value = true;
  message.value = "Closing old MetalSharp, stopping the backend, and launching the updated app...";
  try {
    const result = await window.metalsharp.restartAfterMigration();
    if (result?.ok) return;
    launching.value = false;
    error.value = result?.error ?? "Failed to launch the updated MetalSharp app";
    message.value = `Error: ${error.value}`;
  } catch (e: unknown) {
    launching.value = false;
    error.value = e instanceof Error ? e.message : "Failed to launch the updated MetalSharp app";
    message.value = `Error: ${error.value}`;
  }
}

onMounted(async () => {
  startSpinnerWatchdog();
  await startMigration();
});

onUnmounted(() => {
  stopPolling();
  stopSpinnerWatchdog();
});
</script>

<template>
  <div class="migration-overlay">
    <div class="migration-card">
      <div class="migration-header">
        <div :key="spinnerEpoch" class="loading-icon" :class="{ complete, error: !!error }" aria-hidden="true" />
        <h1 class="migration-title">{{ title }}</h1>
        <p class="migration-subtitle">
          Your game files, settings, bottles, and prefixes stay in place while runtime components are refreshed.
        </p>
      </div>

      <div class="stage-list" aria-label="Migration stages">
        <div
          v-for="(stage, i) in stages"
          :key="i"
          class="migration-stage"
          :class="stageClasses(i)"
          :aria-current="currentStage === i && !complete ? 'step' : undefined"
        >
          <span class="stage-name">{{ stage.name }}</span>
          <div class="stage-marker" aria-hidden="true">
            <IconCheck v-if="stageCompleted(i) && (complete || currentStage !== i)" width="14" height="14" />
            <span v-else>{{ i + 1 }}</span>
          </div>
          <span class="stage-detail">{{ stage.detail }}</span>
        </div>
      </div>

      <div class="progress-section">
        <div class="progress-bar-track">
          <div class="progress-bar-fill" :style="{ width: percent + '%' }" :class="{ complete, error: !!error }" />
        </div>
        <div class="progress-info">
          <span class="progress-percent">{{ percent }}%</span>
          <span v-if="total > 0" class="progress-step">Step {{ step }}/{{ total }}</span>
        </div>
      </div>

      <div class="status-panel" :class="{ error: !!error, complete }" role="status" aria-live="polite">
        <p class="status-message">{{ message }}</p>
      </div>

      <p v-if="!complete && !error" class="migration-note">
        <IconInfo width="14" height="14" aria-hidden="true" />
        Keep MetalSharp open. Large runtime updates and Wine prefix upgrades can take several minutes.
      </p>

      <button v-if="complete" class="restart-btn" :disabled="launching" @click="restartApp()">
        {{ launching ? "Launching..." : "Launch MetalSharp" }}
      </button>
      <button v-if="error && !complete" class="restart-btn" :disabled="retrying" @click="retryMigration()">
        {{ retrying ? "Retrying..." : "Retry Migration" }}
      </button>
      <p v-if="error" class="error-hint">
        The backend will be restarted automatically. If retry still fails, check the logs.
      </p>
    </div>
  </div>
</template>

<style scoped>
.migration-overlay {
  position: fixed;
  inset: 0;
  background: #101d29;
  display: flex;
  align-items: center;
  justify-content: center;
  z-index: 9999;
}

.migration-card {
  width: 100%;
  height: 100%;
  box-sizing: border-box;
  padding: 34px;
  color: #f4f8fb;
  background: #101d29;
  border: 0;
  border-radius: 0;
  box-shadow: none;
}

.migration-header {
  text-align: center;
  margin-bottom: 26px;
}

.loading-icon {
  width: 34px;
  height: 34px;
  margin: 0 auto 18px auto;
  border: 2px solid rgba(102, 192, 244, 0.18);
  border-top-color: #66c0f4;
  border-radius: 50%;
  animation: spin 0.9s linear infinite;
}

.loading-icon.complete {
  animation: none;
  border-color: #4caf50;
}

.loading-icon.error {
  animation: none;
  border-color: #ef5350;
}

.migration-title {
  font-size: 25px;
  font-weight: 700;
  color: #fff;
  margin: 0 0 8px 0;
}

.migration-subtitle {
  font-size: 14px;
  line-height: 1.5;
  color: rgba(226, 239, 247, 0.7);
  max-width: 510px;
  margin: 0 auto;
}

.stage-list {
  display: grid;
  grid-template-columns: repeat(4, minmax(0, 1fr));
  gap: 8px;
  margin-bottom: 26px;
}

.migration-stage {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: flex-start;
  gap: 5px;
  min-width: 0;
  min-height: 86px;
  box-sizing: border-box;
  padding: 10px 8px 8px;
  text-align: center;
  border: 1px solid rgba(255, 255, 255, 0.07);
  border-radius: 10px;
  background: rgba(255, 255, 255, 0.025);
  transition:
    border-color 180ms ease,
    background 180ms ease;
}

.migration-stage.active {
  border-color: rgba(102, 192, 244, 0.5);
  background: rgba(102, 192, 244, 0.1);
}

.migration-stage.completed {
  border-color: rgba(80, 190, 125, 0.24);
}

.migration-stage.pending {
  opacity: 0.55;
}

@keyframes spin {
  to {
    transform: rotate(360deg);
  }
}

.stage-marker {
  width: 24px;
  height: 24px;
  flex: 0 0 24px;
  display: grid;
  place-items: center;
  border: 1px solid rgba(255, 255, 255, 0.14);
  border-radius: 50%;
  color: rgba(255, 255, 255, 0.62);
  font-size: 11px;
  font-weight: 700;
}

.active .stage-marker {
  color: #66c0f4;
  border-color: #66c0f4;
}

.completed .stage-marker {
  color: #79d59d;
  border-color: rgba(80, 190, 125, 0.55);
  background: rgba(80, 190, 125, 0.1);
}

.stage-name {
  font-size: 11px;
  font-weight: 650;
  color: rgba(255, 255, 255, 0.9);
  white-space: nowrap;
  line-height: 1.2;
}

.stage-detail {
  font-size: 9px;
  line-height: 1.25;
  color: rgba(255, 255, 255, 0.42);
  text-align: center;
}

.progress-section {
  margin-bottom: 16px;
}

.progress-bar-track {
  width: 100%;
  height: 6px;
  background: rgba(255, 255, 255, 0.06);
  border-radius: 3px;
  overflow: hidden;
}

.progress-bar-fill {
  height: 100%;
  background: #66c0f4;
  border-radius: 3px;
  transition: width 0.4s ease;
}

.progress-bar-fill.complete {
  background: #4caf50;
}

.progress-bar-fill.error {
  background: #ef5350;
}

.progress-info {
  display: flex;
  justify-content: space-between;
  margin-top: 8px;
  font-size: 12px;
  color: rgba(255, 255, 255, 0.5);
}

.status-panel {
  min-height: 42px;
  display: flex;
  align-items: center;
  justify-content: center;
  padding: 9px 12px;
  margin-top: 16px;
  border-radius: 9px;
  background: rgba(255, 255, 255, 0.035);
  border: 1px solid rgba(255, 255, 255, 0.06);
}

.status-message {
  font-size: 13px;
  line-height: 1.4;
  color: rgba(255, 255, 255, 0.6);
  margin: 0;
}

.status-panel.complete .status-message {
  color: #4caf50;
}

.status-panel.error .status-message {
  color: #ef5350;
}

.migration-note {
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 7px;
  margin: 13px 0 0;
  color: rgba(255, 255, 255, 0.42);
  font-size: 11px;
  line-height: 1.35;
  text-align: center;
}

.restart-btn {
  background: #66c0f4;
  color: #1b2838;
  border: none;
  border-radius: 8px;
  padding: 10px 28px;
  font-size: 14px;
  font-weight: 600;
  cursor: pointer;
  transition: background 0.2s;
  display: block;
  margin: 20px auto 0;
}

.restart-btn:hover {
  background: #4db8e8;
}

.error-hint {
  font-size: 12px;
  color: rgba(255, 255, 255, 0.3);
  margin-top: 12px;
}

@media (max-width: 620px) {
  .migration-card {
    padding: 26px 20px;
  }

  .stage-list {
    grid-template-columns: repeat(2, minmax(0, 1fr));
  }
}
</style>
