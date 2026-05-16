<script setup lang="ts">
import { ref, onMounted, onUnmounted, computed } from "vue";

const status = ref("idle");
const step = ref(0);
const total = ref(0);
const message = ref("Checking migration status...");
const error = ref<string | null>(null);
const complete = ref(false);

let pollTimer: ReturnType<typeof setInterval> | null = null;

const percent = computed(() => {
  if (total.value === 0) return 0;
  return Math.round((step.value / total.value) * 100);
});

const stages = [
  { name: "D3D11", color: "#66c0f4" },
  { name: "Metal", color: "#4fc3f7" },
  { name: "DXGI", color: "#29b6f6" },
  { name: "DXMT", color: "#03a9f4" },
  { name: "FNA", color: "#039be5" },
];

async function startMigration() {
  try {
    const res = await window.metalsharp.request("POST", "/update/migrate/start");
    if (res?.ok) {
      message.value = "Migration started...";
      startPolling();
    } else {
      error.value = res?.error ?? "Failed to start migration";
      message.value = `Error: ${error.value}`;
    }
  } catch (e: any) {
    error.value = e.message ?? "Network error";
    message.value = `Error: ${error.value}`;
  }
}

async function pollProgress() {
  try {
    const res = await window.metalsharp.request("GET", "/update/migrate/progress");
    if (!res) return;
    const data = res.data ?? res;
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
    }
  } catch {}
}

function startPolling() {
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

      <p class="status-message" :class="{ error: !!error, complete }">{{ message }}</p>

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
  max-width: 480px;
  padding: 48px 40px;
}

.migration-header {
  margin-bottom: 40px;
}

.migration-title {
  font-size: 24px;
  font-weight: 700;
  color: #fff;
  margin: 0 0 8px 0;
  letter-spacing: -0.02em;
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
  margin-bottom: 36px;
}

.pipeline-stage {
  display: flex;
  align-items: center;
  gap: 0;
}

.stage-label {
  font-family: "SF Mono", "Menlo", monospace;
  font-size: 11px;
  font-weight: 600;
  color: #66c0f4;
  background: rgba(102, 192, 244, 0.08);
  border: 1px solid rgba(102, 192, 244, 0.2);
  border-radius: 6px;
  padding: 4px 10px;
  transition: all 0.3s ease;
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

.status-message {
  font-size: 13px;
  color: rgba(255, 255, 255, 0.6);
  margin: 0 0 24px 0;
  min-height: 20px;
}

.status-message.complete {
  color: #4caf50;
}

.status-message.error {
  color: #ef5350;
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
