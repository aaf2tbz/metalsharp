<script setup lang="ts">
import { ref, onMounted, onUnmounted, computed } from "vue";

const status = ref("idle");
const step = ref(0);
const total = ref(0);
const message = ref("Checking migration status...");
const error = ref<string | null>(null);
const complete = ref(false);

let pollTimer: ReturnType<typeof setInterval> | null = null;
let pollFailures = 0;
let lastProgressSignature = "";
let lastProgressAt = Date.now();

const MAX_POLL_FAILURES = 20;
const STALE_PROGRESS_MS = 7 * 60 * 1000;

const percent = computed(() => {
  if (total.value === 0) return 0;
  return Math.round((step.value / total.value) * 100);
});

const busy = computed(() => !complete.value && !error.value);

const stages = [
  { name: "[Wine]" },
  { name: "[DXMT]" },
  { name: "[x86_x64]" },
  { name: "[Metal]" },
];

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
  try {
    const res = await window.metalsharp.migrateProgress();
    if (!res) return;
    pollFailures = 0;
    const data = res.data ?? res;
    const signature = `${data.status ?? "idle"}:${data.step ?? 0}:${data.total ?? 0}:${data.message ?? ""}:${data.error ?? ""}`;
    if (signature !== lastProgressSignature) {
      lastProgressSignature = signature;
      lastProgressAt = Date.now();
    }
    status.value = data.status ?? "idle";
    step.value = data.step ?? 0;
    total.value = data.total ?? 0;
    message.value = data.message ?? "";
    error.value = data.error ?? null;

    if (status.value === "complete") {
      complete.value = true;
      stopPolling();
    } else if (status.value === "error") {
      stopPolling();
    } else if (Date.now() - lastProgressAt > STALE_PROGRESS_MS) {
      error.value = "migration_stalled";
      message.value = "Migration stopped reporting progress. Restart MetalSharp and run setup repair.";
      stopPolling();
    }
  } catch (e: unknown) {
    pollFailures += 1;
    if (pollFailures >= MAX_POLL_FAILURES) {
      error.value = e instanceof Error ? e.message : "Migration backend unavailable";
      message.value = `Error: ${error.value}`;
      stopPolling();
    }
  }
}

function startPolling() {
  stopPolling();
  void pollProgress();
  pollTimer = setInterval(pollProgress, 500);
}

function stopPolling() {
  if (pollTimer) {
    clearInterval(pollTimer);
    pollTimer = null;
  }
}

async function restartApp() {
  await window.metalsharp.restartAfterMigration();
}

onMounted(async () => {
  await startMigration();
});

onUnmounted(() => {
  stopPolling();
});
</script>

<template>
  <div class="migration-overlay">
    <div class="migration-card">
      <div class="migration-header">
        <h1 class="migration-title">MetalSharp Migration</h1>
        <p class="migration-subtitle">Upgrading runtime to latest version</p>
      </div>

      <div class="pipeline-vis">
        <div v-for="(stage, i) in stages" :key="stage.name" class="pipeline-stage" :class="{ active: !complete && !error }">
          <span class="stage-label">{{ stage.name }}</span>
          <div v-if="i < stages.length - 1" class="pipeline-arrow">
            <svg width="16" height="12" viewBox="0 0 16 12"><path d="M0 6h12m0 0l-4-4m4 4l-4 4" stroke="currentColor" stroke-width="1.5" fill="none" /></svg>
          </div>
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

      <div class="status-row" :class="{ error: !!error, complete }">
        <span v-if="busy" class="step-spinner" aria-hidden="true"></span>
        <p class="status-message">{{ message }}</p>
      </div>

      <button v-if="complete" class="restart-btn" @click="restartApp()">Restart MetalSharp</button>
      <p v-if="error" class="error-hint">Try restarting the app. If the issue persists, check the logs.</p>
    </div>
  </div>
</template>

<style scoped>
.migration-overlay {
  position: fixed;
  inset: 0;
  background: #1b2838;
  display: flex;
  align-items: center;
  justify-content: center;
  z-index: 9999;
}

.migration-card {
  text-align: center;
  max-width: 420px;
  padding: 34px 32px;
}

.migration-header {
  margin-bottom: 28px;
}

.migration-title {
  font-size: 21px;
  font-weight: 700;
  color: #fff;
  margin: 0 0 8px 0;
  letter-spacing: 0;
}

.migration-subtitle {
  font-size: 14px;
  color: #66c0f4;
  opacity: 0.8;
  margin: 0;
}

.pipeline-vis {
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 0;
  margin-bottom: 26px;
}

.pipeline-stage {
  display: flex;
  align-items: center;
  gap: 0;
}

.stage-label {
  font-family: "SF Mono", "Menlo", monospace;
  font-size: 10px;
  font-weight: 600;
  color: #66c0f4;
  background: rgba(102, 192, 244, 0.08);
  border: 1px solid rgba(102, 192, 244, 0.2);
  border-radius: 5px;
  padding: 4px 7px;
  transition: all 0.3s ease;
  white-space: nowrap;
}

.pipeline-stage.active .stage-label {
  animation: pulse 2s ease-in-out infinite;
  background: rgba(102, 192, 244, 0.15);
  border-color: rgba(102, 192, 244, 0.4);
}

@keyframes pulse {
  0%, 100% { opacity: 0.6; }
  50% { opacity: 1; }
}

.pipeline-arrow {
  color: rgba(102, 192, 244, 0.3);
  margin: 0 2px;
}

.progress-section {
  margin-bottom: 14px;
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

.status-row {
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 9px;
  min-height: 22px;
  margin: 0 0 22px 0;
  color: rgba(255, 255, 255, 0.6);
}

.status-message {
  font-size: 13px;
  color: inherit;
  margin: 0;
  line-height: 1.35;
}

.status-row.complete {
  color: #4caf50;
}

.status-row.error {
  color: #ef5350;
}

.step-spinner {
  width: 14px;
  height: 14px;
  border: 2px solid rgba(102, 192, 244, 0.22);
  border-top-color: #66c0f4;
  border-radius: 50%;
  flex: 0 0 auto;
  animation: spin 0.8s linear infinite;
}

@keyframes spin {
  to { transform: rotate(360deg); }
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
}

.restart-btn:hover {
  background: #4db8e8;
}

.error-hint {
  font-size: 12px;
  color: rgba(255, 255, 255, 0.3);
  margin-top: 12px;
}
</style>
