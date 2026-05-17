<script setup lang="ts">
import { ref, onMounted } from "vue";
import { useToast } from "../composables/useToast";
import { api, getAPI } from "../composables/useApi";
import type { SharpApp } from "../api-types";

const toast = useToast();
const apps = ref<SharpApp[]>([]);
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
  const result = await api<{ ok: boolean; apps: SharpApp[] }>("GET", "/sharp-library");
  if (result?.ok) apps.value = result.apps;
}

async function installExe() {
  const filePath = await getAPI().pickExeFile();
  if (!filePath) return;
  toast.show("Installing application...");
  const result = await api<{ ok: boolean; app?: SharpApp; error?: string }>("POST", "/sharp-library/install", { srcPath: filePath });
  if (result?.ok && result.app) {
    toast.show(`Installed ${result.app.name}`, "success");
    await load();
  } else {
    toast.show(result?.error ?? "Failed to install", "error");
  }
}

async function launchApp(id: string, engine: string) {
  const app = apps.value.find((a) => a.id === id);
  if (!app) return;
  toast.show(`Launching ${app.name}...`);
  const result = await api<{ ok: boolean; pid?: number; pipeline?: string; warnings?: string[]; error?: string }>("POST", "/sharp-library/launch", { id, engine });
  if (result?.ok && result.pid) {
    const warning = result.warnings?.[0];
    toast.show(warning ? `Launched ${app.name}: ${warning}` : `Launched ${app.name}`, "success");
  }
  else toast.show(result?.error ?? `Failed to launch ${app.name}`, "error");
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
  if (result?.ok) { toast.show(`Uninstalled ${app.name}`, "success"); await load(); }
  else toast.show(result?.error ?? "Failed to uninstall", "error");
}

async function setCover(id: string) {
  const filePath = await getAPI().pickImageFile();
  if (!filePath) return;
  const result = await api<{ ok: boolean; error?: string }>("POST", "/sharp-library/set-cover", { id, coverPath: filePath });
  if (result?.ok) { toast.show("Cover updated", "success"); await load(); }
  else toast.show(result?.error ?? "Failed to set cover", "error");
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
      <button class="btn btn-primary" @click="installExe">Install an EXE</button>
    </div>

    <div v-if="apps.length === 0" class="empty-state">
      <div class="empty-icon">
        <svg width="48" height="48" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5"><rect x="2" y="3" width="20" height="14" rx="2"/><line x1="8" y1="21" x2="16" y2="21"/><line x1="12" y1="17" x2="12" y2="21"/></svg>
      </div>
      <h2>No applications installed</h2>
      <p>Click "Install an EXE" to add a Windows application</p>
    </div>

    <div v-else class="sharp-grid">
      <div v-for="app in apps" :key="app.id" class="sharp-card">
        <div class="sharp-card-banner">
          <img v-if="app.cover" :src="`http://127.0.0.1:9274/sharp-library/cover?id=${app.id}`" :alt="app.name" />
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
              <select class="control-input" :value="app.engine" @change="updateEngine(app.id, ($event.target as HTMLSelectElement).value)">
                <option v-for="option in engineOptions" :key="option.id" :value="option.id">
                  {{ option.name }}
                </option>
              </select>
            </div>
            <div class="sharp-card-actions-row subtle">
              <button class="btn btn-secondary btn-sm" @click="setCover(app.id)">Set Cover</button>
              <button class="btn btn-danger btn-sm" @click="uninstallApp(app.id)">Uninstall</button>
            </div>
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
  margin: -24px -28px 20px;
  padding: 24px 28px 18px;
  background: var(--page-header-bg);
  border-bottom: 1px solid var(--border);
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
}

.empty-state {
  text-align: center;
  padding: 80px 20px;
  color: var(--text-dim);
}
.empty-icon { margin-bottom: 16px; opacity: 0.4; }
.empty-state h2 { font-size: 16px; margin-bottom: 8px; color: var(--text-secondary); }
.empty-state p { font-size: 13px; }
</style>
