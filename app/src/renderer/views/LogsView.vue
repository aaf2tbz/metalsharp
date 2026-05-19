<script setup lang="ts">
import { ref, onMounted, onUnmounted } from "vue";
import { api, getAPI } from "../composables/useApi";

const logs = ref<string[]>([]);
const logFiles = ref<{ name: string; lines: string[] }[]>([]);
const crashReports = ref<{ file: string; name: string; source: string; timestamp: string; size_bytes: number }[]>([]);
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
  crashReports.value = result.reports.slice(0, 20);
}

async function loadLogFiles() {
  const result = await api<{ ok: boolean; logs: { name: string; lines: string[] }[] }>("GET", "/logs");
  if (result?.ok) logFiles.value = result.logs;
}

function clearView() {
  logs.value = [];
  logFiles.value = [];
  crashReports.value = [];
  logLineCount.value = 0;
}

async function openLogFolder() {
  await getAPI().openInFinder("~/.metalsharp/logs");
}

onMounted(async () => {
  await pollLogs();
  await loadCrashReports();
  await loadLogFiles();
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
    <div class="log-drawers">
      <details v-if="crashReports.length" class="log-drawer" open>
        <summary>
          Crash reports <span>{{ crashReports.length }}</span>
        </summary>
        <div v-for="report in crashReports" :key="report.file" class="report-row">
          <strong>{{ report.name }}</strong>
          <span>{{ report.source }} - {{ report.timestamp }} - {{ formatBytes(report.size_bytes) }}</span>
          <small>{{ report.file }}</small>
        </div>
      </details>
      <details v-if="logFiles.length" class="log-drawer">
        <summary>
          Recent log files <span>{{ logFiles.length }}</span>
        </summary>
        <div v-for="entry in logFiles" :key="entry.name" class="file-log">
          <div class="file-log-name">{{ entry.name }}</div>
          <pre>{{ entry.lines.slice(-40).join("\n") }}</pre>
        </div>
      </details>
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
    formatBytes(bytes: number): string {
      if (bytes < 1024) return `${bytes} B`;
      if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
      if (bytes < 1024 * 1024 * 1024) return `${(bytes / (1024 * 1024)).toFixed(1)} MB`;
      return `${(bytes / (1024 * 1024 * 1024)).toFixed(1)} GB`;
    },
    logClass(line: string): string {
      if (line.includes("[LAUNCH]") || line.includes("[LAUNCHED]")) return "log-event-launch";
      if (line.includes("[STOP]") || line.includes("[STOPPED]")) return "log-event-stop";
      if (line.includes("[STOP FAILED]") || line.includes("[LAUNCH FAILED]")) return "log-event-error";
      if (line.includes("engine:")) return "log-event-engine";
      if (
        line.toLowerCase().includes("crash") ||
        line.toLowerCase().includes("error") ||
        line.toLowerCase().includes("failed")
      )
        return "log-event-warn";
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
  margin: -24px -28px 16px;
  padding: 24px 28px 18px;
  background: var(--page-header-bg);
  border-bottom: 1px solid var(--border);
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

.log-drawers {
  display: flex;
  flex-direction: column;
  gap: 10px;
  margin-bottom: 12px;
}
.log-drawer {
  border: 1px solid var(--border);
  border-radius: var(--radius-md);
  background: var(--bg-surface);
  color: var(--text-secondary);
}
.log-drawer summary {
  display: flex;
  justify-content: space-between;
  gap: 12px;
  padding: 10px 12px;
  cursor: pointer;
  font-size: 12px;
  font-weight: 700;
  list-style: none;
}
.log-drawer summary::-webkit-details-marker {
  display: none;
}
.log-drawer summary::after {
  content: "v";
  color: var(--text-dim);
}
.log-drawer:not([open]) summary::after {
  transform: rotate(-90deg);
}
.log-drawer summary span {
  margin-left: auto;
  color: var(--text-dim);
  font-weight: 500;
}
.report-row {
  display: flex;
  flex-direction: column;
  gap: 2px;
  padding: 8px 12px;
  border-top: 1px solid var(--border);
  font-size: 12px;
}
.report-row span,
.report-row small {
  color: var(--text-dim);
}
.report-row small {
  overflow-wrap: anywhere;
}
.file-log {
  padding: 10px 12px;
  border-top: 1px solid var(--border);
}
.file-log-name {
  margin-bottom: 6px;
  color: var(--text-secondary);
  font-size: 12px;
  font-weight: 700;
}
.file-log pre {
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
  line-height: 1.5;
  white-space: pre-wrap;
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
