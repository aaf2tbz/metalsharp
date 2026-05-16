<script setup lang="ts">
import { ref, onMounted, onUnmounted } from "vue";
import { api, getAPI } from "../composables/useApi";

const logs = ref<string[]>([]);
const logLineCount = ref(0);
let pollInterval: ReturnType<typeof setInterval> | null = null;

async function pollLogs() {
  const result = await api<{ ok: boolean; total: number; lines: string[] }>(
    "GET",
    `/logs/stream?after=${logLineCount.value}`,
  );
  if (result?.ok && result.lines?.length) {
    logs.value.push(...result.lines);
  }
  if (result?.ok) logLineCount.value = result.total;
}

async function loadCrashReports() {
  const result = await api<{
    ok: boolean;
    reports: { file: string; name: string; source: string; timestamp: string; size_bytes: number }[];
  }>("GET", "/logs/crash-reports");
  if (!result?.ok || !result.reports?.length) return;
  logs.value.push(`--- ${result.reports.length} crash dump(s) detected ---`);
  for (const r of result.reports.slice(0, 10)) {
    logs.value.push(`${r.name} (${r.source}) — ${r.timestamp}`);
  }
}

function clearView() {
  logs.value = [];
  logLineCount.value = 0;
}

async function openLogFolder() {
  await getAPI().openInFinder("~/.metalsharp/logs");
}

onMounted(async () => {
  await pollLogs();
  await loadCrashReports();
  pollInterval = setInterval(pollLogs, 2000);
});

onUnmounted(() => {
  if (pollInterval) clearInterval(pollInterval);
});
</script>

<template>
  <div class="logs-view">
    <div class="logs-header">
      <div>
        <h1>Logs</h1>
        <p class="subtitle">Live MetalSharp runtime logs</p>
      </div>
      <div class="logs-actions">
        <button class="btn btn-secondary" @click="openLogFolder">Open Logs</button>
        <button class="btn btn-secondary" @click="clearView">Clear View</button>
      </div>
    </div>
    <div class="log-content">
      <div v-for="(line, i) in logs" :key="i" class="log-line" :class="logClass(line)">
        {{ line }}
      </div>
    </div>
  </div>
</template>

<script lang="ts">
export default {
  methods: {
    logClass(line: string): string {
      if (line.includes("[LAUNCH]") || line.includes("[LAUNCHED]")) return "log-event-launch";
      if (line.includes("[STOP]") || line.includes("[STOPPED]")) return "log-event-stop";
      if (line.includes("[STOP FAILED]") || line.includes("[LAUNCH FAILED]")) return "log-event-error";
      if (line.includes("engine:")) return "log-event-engine";
      if (line.toLowerCase().includes("crash") || line.toLowerCase().includes("error") || line.toLowerCase().includes("failed")) return "log-event-warn";
      return "";
    },
  },
};
</script>

<style scoped>
.logs-view {
  padding: 24px 28px;
  height: 100%;
  display: flex;
  flex-direction: column;
}
.logs-header {
  display: flex;
  justify-content: space-between;
  align-items: flex-start;
  margin-bottom: 16px;
}
.logs-header h1 {
  font-size: 22px;
  font-weight: 600;
}
.subtitle {
  font-size: 12px;
  color: var(--text-dim);
  margin-top: 2px;
}
.logs-actions {
  display: flex;
  gap: 8px;
}

.log-content {
  flex: 1;
  overflow-y: auto;
  background: var(--bg-deep);
  border: 1px solid var(--border);
  border-radius: var(--radius-md);
  padding: 14px 16px;
  font-family: var(--font-mono);
  font-size: 12px;
  line-height: 1.8;
  color: var(--text-secondary);
  white-space: pre-wrap;
}

.log-line {
  word-break: break-word;
}
.log-line.log-event-launch {
  color: var(--success);
}
.log-line.log-event-stop {
  color: var(--warn);
}
.log-line.log-event-error {
  color: var(--error);
}
.log-line.log-event-engine {
  color: var(--accent);
}
.log-line.log-event-warn {
  color: var(--warn);
}
</style>
