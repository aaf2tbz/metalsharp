<script setup lang="ts">
import { ref, inject, type Ref } from "vue";
import { useToast } from "../composables/useToast";
import { api, getAPI } from "../composables/useApi";

const emit = defineEmits<{ done: [] }>();
const toast = useToast();
const library = inject<Ref<{ ok: boolean; total: number; installed_count: number; games: unknown[] } | null>>("library", ref(null));
const steamApiKey = inject<Ref<string | null>>("steamApiKey", ref(null));

const step = ref(0);
const deviceName = ref("");
const installProgress = ref(0);
const installStatus = ref("");
const installing = ref(false);
const installLogs = ref<{ text: string; cls: string }[]>([]);
const steamInstalled = ref(false);
const steamInstalling = ref(false);
const installingSteam = ref(false);

const steps = ["Welcome", "Install Runtime", "Done"];

async function startInstall() {
  installing.value = true;
  installLogs.value = [];
  installProgress.value = 0;

  const started = await api<{ ok: boolean; error?: string }>("POST", "/setup/install-all");
  if (!started?.ok) {
    toast.show(started?.error ?? "Failed to start installation", "error");
    installing.value = false;
    return;
  }

  installLogs.value.push({ text: "Starting installation...", cls: "info" });

  let lastStep = -1;
  let lastStatusText = "";

  const poll = setInterval(async () => {
    const progress = await api<{
      step: number;
      total: number;
      current: string;
      status: string;
      log: string;
      error: string | null;
    }>("GET", "/setup/install-progress");
    if (!progress) return;

    const pct = progress.total > 0 ? Math.round((progress.step / progress.total) * 100) : 0;
    installProgress.value = pct;

    if (progress.step !== lastStep || progress.status !== lastStatusText) {
      if (progress.status === "done" || progress.status === "skipped") {
        installLogs.value.push({ text: progress.log, cls: progress.status === "done" ? "success" : "warn" });
      } else if (progress.status === "error") {
        installLogs.value.push({ text: progress.log, cls: "error" });
        if (progress.error) installLogs.value.push({ text: `Error: ${progress.error}`, cls: "error" });
        clearInterval(poll);
        installing.value = false;
        return;
      } else if (progress.status === "installing" && progress.step !== lastStep) {
        installLogs.value.push({ text: progress.log, cls: "active" });
      } else if (progress.status === "complete") {
        installLogs.value.push({ text: "Runtime installed!", cls: "success" });
        clearInterval(poll);
        installProgress.value = 100;
        installing.value = false;
        installStatus.value = "complete";
        toast.show("Runtime installed!", "success");
        checkSteam();
      }
    }

    lastStep = progress.step;
    lastStatusText = progress.status;
  }, 500);
}

async function checkSteam() {
  const s = await api<{ installed: boolean; running: boolean }>("GET", "/steam/status");
  if (s?.installed || s?.running) {
    steamInstalled.value = true;
  }
  installingSteam.value = true;
}

async function installSteam() {
  steamInstalling.value = true;
  const result = await api<{ ok: boolean; error?: string }>("POST", "/steam/install");
  if (!result?.ok) {
    toast.show(result?.error ?? "Failed to install Steam", "error");
    steamInstalling.value = false;
    return;
  }
  const poll = setInterval(async () => {
    const s = await api<{ installed: boolean; running: boolean }>("GET", "/steam/status");
    if (s?.installed || s?.running) {
      clearInterval(poll);
      steamInstalled.value = true;
      steamInstalling.value = false;
    }
  }, 3000);
  setTimeout(() => {
    clearInterval(poll);
    steamInstalling.value = false;
  }, 300000);
}

async function finish() {
  const keyInput = document.getElementById("setup-api-key") as HTMLInputElement;
  const nameInput = document.getElementById("setup-device-name") as HTMLInputElement;
  const name = nameInput?.value?.trim() || deviceName.value;
  const key = keyInput?.value?.trim();

  await api("POST", "/setup/save", { step: 2, deviceName: name, completed: true });
  if (key) {
    const result = await api<{
      ok: boolean;
      error?: string;
      library?: { ok: boolean; total: number; installed_count: number; games: unknown[] };
      sync?: { steam_id_detected: boolean };
    }>("POST", "/steam/save-api-key", { key });
    if (!result?.ok) {
      toast.show(result?.error ?? "Failed to save Steam API key", "error");
      return;
    }
    steamApiKey.value = key;
    if (result.library) library.value = result.library;
    if (result.sync && !result.sync.steam_id_detected) {
      toast.show("API key saved, but SteamID was not detected yet", "error");
    }
  }

  emit("done");
}

async function goToStep2() {
  step.value = 2;
  const gen = await api<{ name: string }>("GET", "/setup/device-name");
  if (gen?.name) deviceName.value = gen.name;
}
</script>

<template>
  <div class="setup-overlay">
    <div class="setup-wizard">
      <div class="setup-steps">
        <template v-for="(s, i) in steps" :key="i">
          <div class="setup-step-item" :class="{ done: i < step, current: i === step }">
            <div class="setup-step-dot" :class="{ done: i < step, current: i === step }"></div>
            <span class="setup-step-label" :class="{ current: i === step }">{{ s }}</span>
          </div>
          <div v-if="i < steps.length - 1" class="setup-step-line" :class="{ done: i < step }"></div>
        </template>
      </div>

      <div v-if="step === 0" class="setup-body">
        <div class="setup-hero">
          <div class="setup-hero-icon">M</div>
          <h1 class="setup-hero-title">Welcome to MetalSharp</h1>
          <p class="setup-hero-sub">Run Windows games natively on macOS with Metal. No VM, no Vulkan.</p>
        </div>
        <div class="setup-features">
          <div class="setup-feature">
            <div class="setup-feature-icon">
              <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><polygon points="13 2 3 14 12 14 11 22 21 10 12 10 13 2" /></svg>
            </div>
            <div>
              <div class="setup-feature-title">Direct3D to Metal</div>
              <div class="setup-feature-desc">Single-hop translation — faster than DXVK+MoltenVK</div>
            </div>
          </div>
          <div class="setup-feature">
            <div class="setup-feature-icon">
              <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><rect x="2" y="6" width="20" height="12" rx="2"/><line x1="6" y1="10" x2="6" y2="14"/><line x1="10" y1="10" x2="10" y2="14"/></svg>
            </div>
            <div>
              <div class="setup-feature-title">XNA/FNA Native</div>
              <div class="setup-feature-desc">Run Celeste, Terraria, and other XNA games via FNA + SDL3 + Metal</div>
            </div>
          </div>
          <div class="setup-feature">
            <div class="setup-feature-icon">
              <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><rect x="3" y="11" width="18" height="11" rx="2"/><path d="M7 11V7a5 5 0 0 1 10 0v4" /></svg>
            </div>
            <div>
              <div class="setup-feature-title">Steam Integration</div>
              <div class="setup-feature-desc">Browse your library, download games, launch with one click</div>
            </div>
          </div>
        </div>
        <div class="setup-actions">
          <button class="btn btn-primary btn-lg" @click="step = 1">Get Started</button>
        </div>
      </div>

      <div v-if="step === 1" class="setup-body">
        <div class="setup-section-header">
          <h1>Install Runtime</h1>
          <p>MetalSharp needs the Wine runtime and game assets. One click installs everything.</p>
        </div>

        <button
          v-if="installStatus !== 'complete'"
          class="btn btn-primary btn-lg"
          :disabled="installing"
          @click="startInstall"
        >
          {{ installing ? "Installing..." : "Install Runtime" }}
        </button>

        <div v-if="installing || installLogs.length > 0" class="setup-progress-section">
          <div class="setup-progress-bar-container">
            <div class="setup-progress-bar" :style="{ width: installProgress + '%' }"></div>
            <span class="setup-progress-label">{{ installProgress }}%</span>
          </div>
          <div class="setup-log">
            <div v-for="(log, i) in installLogs" :key="i" class="setup-log-line" :class="log.cls">
              {{ log.text }}
            </div>
          </div>
        </div>

        <div v-if="installStatus === 'complete'" class="setup-steam-section">
          <h2>Steam</h2>
          <p>Install Windows Steam to download and play games.</p>
          <span v-if="steamInstalled" class="badge badge-ok" style="font-size:13px;padding:10px 20px;">Steam installed</span>
          <button v-else class="btn btn-primary" :disabled="steamInstalling" @click="installSteam">
            {{ steamInstalling ? "Installing Steam..." : "Install Steam" }}
          </button>
        </div>

        <div class="setup-actions">
          <button class="btn btn-secondary" @click="step = 0">Back</button>
          <button v-if="installStatus === 'complete'" class="btn btn-primary btn-lg" @click="goToStep2">Finish Setup</button>
        </div>
      </div>

      <div v-if="step === 2" class="setup-body">
        <div class="setup-complete">
          <div class="setup-complete-icon">
            <svg width="36" height="36" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><polyline points="20 6 9 17 4 12" /></svg>
          </div>
          <h1>You're All Set!</h1>
          <p>MetalSharp is ready. Open your library, start Steam, and download games.</p>
          <div class="setup-form">
            <div class="setup-form-group">
              <label class="setup-label">Device Name</label>
              <input id="setup-device-name" type="text" :value="deviceName" placeholder="e.g. Swift-Falcon" class="setup-input" />
              <div class="setup-hint">Identifies your machine to Steam for persistent login.</div>
            </div>
            <div class="setup-form-group">
              <label class="setup-label">Steam Web API Key (optional)</label>
              <input id="setup-api-key" type="password" placeholder="Enter your Steam Web API key..." class="setup-input" />
              <div class="setup-hint">
                Loads your full game library. Get a free key at
                <a href="https://steamcommunity.com/dev/apikey" target="_blank">steamcommunity.com/dev/apikey</a>
              </div>
            </div>
          </div>
          <div class="setup-tips">
            <div class="setup-tip"><strong>Start Steam</strong> — Click "Start Steam" in your Library, then log in through the Steam window.</div>
            <div class="setup-tip"><strong>Download a game</strong> — Find it in your Library and click Install.</div>
            <div class="setup-tip"><strong>First launch</strong> — MetalSharp auto-configures the runtime for each game.</div>
          </div>
          <div class="setup-actions" style="justify-content:center;margin-top:32px;">
            <button class="btn btn-primary btn-lg" @click="finish">Launch MetalSharp</button>
          </div>
        </div>
      </div>
    </div>
  </div>
</template>

<style scoped>
.setup-overlay {
  position: fixed;
  inset: 0;
  z-index: 1000;
  display: flex;
  align-items: center;
  justify-content: center;
  background: var(--bg-deep);
}

.setup-wizard {
  width: 540px;
  max-width: 94vw;
  max-height: 90vh;
  overflow-y: auto;
  background: var(--bg-surface);
  border: 1px solid var(--border);
  border-radius: var(--radius-lg);
  padding: 32px;
}

.setup-steps {
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 0;
  margin-bottom: 32px;
}
.setup-step-item {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 4px;
}
.setup-step-dot {
  width: 12px;
  height: 12px;
  border-radius: 50%;
  background: var(--border-strong);
  transition: all var(--transition);
}
.setup-step-dot.done {
  background: var(--success);
}
.setup-step-dot.current {
  background: var(--accent);
  box-shadow: 0 0 0 4px var(--accent-glow);
}
.setup-step-label {
  font-size: 10px;
  color: var(--text-dim);
}
.setup-step-label.current {
  color: var(--accent);
  font-weight: 600;
}
.setup-step-line {
  width: 48px;
  height: 2px;
  background: var(--border-strong);
  margin: 0 8px;
  margin-bottom: 18px;
  transition: background var(--transition);
}
.setup-step-line.done {
  background: var(--success);
}

.setup-body {
  text-align: center;
}

.setup-hero {
  margin-bottom: 32px;
}
.setup-hero-icon {
  font-family: var(--font-logo);
  font-size: 28px;
  color: var(--accent);
  margin-bottom: 16px;
}
.setup-hero-title {
  font-size: 24px;
  font-weight: 700;
  margin-bottom: 8px;
}
.setup-hero-sub {
  color: var(--text-secondary);
  font-size: 14px;
  line-height: 1.5;
}

.setup-features {
  display: flex;
  flex-direction: column;
  gap: 12px;
  margin-bottom: 28px;
  text-align: left;
}
.setup-feature {
  display: flex;
  align-items: flex-start;
  gap: 12px;
  padding: 12px;
  background: var(--bg-card);
  border-radius: var(--radius-md);
  border: 1px solid var(--border);
}
.setup-feature-icon {
  color: var(--accent);
  flex-shrink: 0;
  margin-top: 2px;
}
.setup-feature-title {
  font-weight: 600;
  font-size: 13px;
  margin-bottom: 2px;
}
.setup-feature-desc {
  font-size: 12px;
  color: var(--text-dim);
  line-height: 1.4;
}

.setup-actions {
  display: flex;
  justify-content: center;
  gap: 10px;
  margin-top: 24px;
}

.setup-section-header {
  margin-bottom: 20px;
}
.setup-section-header h1 {
  font-size: 20px;
  margin-bottom: 6px;
}
.setup-section-header p {
  color: var(--text-dim);
  font-size: 13px;
}

.setup-progress-section {
  margin-top: 16px;
  text-align: left;
}
.setup-progress-bar-container {
  position: relative;
  height: 24px;
  background: var(--bg-deep);
  border-radius: 12px;
  overflow: hidden;
  margin-bottom: 12px;
}
.setup-progress-bar {
  height: 100%;
  background: var(--accent);
  border-radius: 12px;
  transition: width 0.3s ease;
}
.setup-progress-label {
  position: absolute;
  top: 50%;
  left: 50%;
  transform: translate(-50%, -50%);
  font-size: 11px;
  font-weight: 600;
  color: var(--text-bright);
}
.setup-log {
  max-height: 160px;
  overflow-y: auto;
  font-family: var(--font-mono);
  font-size: 11px;
  line-height: 1.6;
  background: var(--bg-deep);
  border-radius: var(--radius-sm);
  padding: 10px 12px;
}
.setup-log-line {
  color: var(--text-secondary);
}
.setup-log-line.success {
  color: var(--success);
}
.setup-log-line.warn {
  color: var(--warn);
}
.setup-log-line.error {
  color: var(--error);
}
.setup-log-line.active {
  color: var(--accent);
}

.setup-steam-section {
  margin-top: 20px;
  padding-top: 16px;
  border-top: 1px solid var(--border);
}
.setup-steam-section h2 {
  font-size: 16px;
  margin-bottom: 4px;
}
.setup-steam-section p {
  font-size: 13px;
  color: var(--text-dim);
  margin-bottom: 12px;
}

.setup-complete {
  text-align: center;
}
.setup-complete-icon {
  color: var(--success);
  margin-bottom: 12px;
}
.setup-complete h1 {
  font-size: 22px;
  margin-bottom: 8px;
}
.setup-complete > p {
  color: var(--text-secondary);
  font-size: 13px;
  margin-bottom: 24px;
}

.setup-form {
  max-width: 400px;
  margin: 0 auto 24px;
  text-align: left;
}
.setup-form-group {
  margin-bottom: 16px;
}
.setup-label {
  display: block;
  font-size: 12px;
  font-weight: 600;
  margin-bottom: 4px;
}
.setup-input {
  width: 100%;
  background: var(--bg-card);
  color: var(--text-primary);
  border: 1px solid var(--border-strong);
  border-radius: var(--radius-sm);
  padding: 8px 12px;
  font-size: 13px;
  outline: none;
}
.setup-input:focus {
  border-color: var(--accent);
}
.setup-hint {
  font-size: 11px;
  color: var(--text-dim);
  margin-top: 4px;
  line-height: 1.4;
}

.setup-tips {
  text-align: left;
  max-width: 400px;
  margin: 0 auto;
  display: flex;
  flex-direction: column;
  gap: 8px;
}
.setup-tip {
  font-size: 12px;
  color: var(--text-secondary);
  line-height: 1.5;
  padding: 8px 12px;
  background: var(--bg-card);
  border-radius: var(--radius-sm);
  border: 1px solid var(--border);
}
</style>
