<script setup lang="ts">
import { ref } from "vue";

defineProps<{
  currentView: string;
  theme: "dark" | "light";
}>();

const emit = defineEmits<{
  navigate: [view: string];
  toggleTheme: [];
}>();

const collapsed = ref(false);

const navItems = [
  {
    view: "library",
    label: "Library",
    paths: ["M21 6H3a1 1 0 0 0-1 1v4a1 1 0 0 0 1 1h18a1 1 0 0 0 1-1V7a1 1 0 0 0-1-1z", "M21 14H3a1 1 0 0 0-1 1v4a1 1 0 0 0 1 1h18a1 1 0 0 0 1-1v-4a1 1 0 0 0-1-1z"],
  },
  {
    view: "sharp-library",
    label: "Sharp",
    paths: ["M12 2L2 7l10 5 10-5-10-5z", "M2 12l10 5 10-5", "M2 17l10 5 10-5"],
  },
  {
    view: "logs",
    label: "Logs",
    paths: ["M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z", "M14 2v6h6", "M16 13H8", "M16 17H8", "M10 9H8"],
  },
];
</script>

<template>
  <nav class="sidebar" :class="{ collapsed }">
    <div class="sidebar-top">
      <button class="sidebar-hamburger" @click="collapsed = !collapsed" title="Toggle sidebar">
        <svg width="18" height="18" viewBox="0 0 18 18" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round">
          <line x1="3" y1="5" x2="15" y2="5" />
          <line x1="3" y1="9" x2="15" y2="9" />
          <line x1="3" y1="13" x2="15" y2="13" />
        </svg>
      </button>
      <div class="sidebar-logo">
        <img src="../icon.png" alt="M" class="sidebar-logo-icon" />
        <span v-if="!collapsed" class="sidebar-logo-text">MetalSharp</span>
      </div>
    </div>

    <div class="sidebar-nav">
      <button
        v-for="item in navItems"
        :key="item.view"
        class="sidebar-nav-item"
        :class="{ active: currentView === item.view }"
        @click="emit('navigate', item.view)"
        :title="collapsed ? item.label : undefined"
      >
        <svg class="sidebar-nav-icon" width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
          <path v-for="path in item.paths" :key="path" :d="path" />
        </svg>
        <span v-if="!collapsed" class="sidebar-nav-label">{{ item.label }}</span>
      </button>
    </div>

    <div class="sidebar-bottom">
      <button
        class="sidebar-nav-item sidebar-theme-toggle"
        @click="emit('toggleTheme')"
        :title="collapsed ? (theme === 'dark' ? 'Light Mode' : 'Dark Mode') : undefined"
      >
        <svg v-if="theme === 'dark'" class="sidebar-nav-icon" width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
          <path d="M21 12.79A9 9 0 1 1 11.21 3 7 7 0 0 0 21 12.79z" />
        </svg>
        <svg v-else class="sidebar-nav-icon" width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
          <circle cx="12" cy="12" r="5" /><line x1="12" y1="1" x2="12" y2="3" /><line x1="12" y1="21" x2="12" y2="23" /><line x1="4.22" y1="4.22" x2="5.64" y2="5.64" /><line x1="18.36" y1="18.36" x2="19.78" y2="19.78" /><line x1="1" y1="12" x2="3" y2="12" /><line x1="21" y1="12" x2="23" y2="12" /><line x1="4.22" y1="19.78" x2="5.64" y2="18.36" /><line x1="18.36" y1="5.64" x2="19.78" y2="4.22" />
        </svg>
        <span v-if="!collapsed" class="sidebar-nav-label">{{ theme === "dark" ? "Light Mode" : "Dark Mode" }}</span>
      </button>
      <button
        class="sidebar-nav-item"
        :class="{ active: currentView === 'settings' }"
        @click="emit('navigate', 'settings')"
        :title="collapsed ? 'Settings' : undefined"
      >
        <svg class="sidebar-nav-icon" width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
          <circle cx="12" cy="12" r="3" /><path d="M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 0 1-2.83 2.83l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 0 1-4 0v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 0 1-2.83-2.83l.06-.06A1.65 1.65 0 0 0 4.68 15a1.65 1.65 0 0 0-1.51-1H3a2 2 0 0 1 0-4h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 0 1 2.83-2.83l.06.06A1.65 1.65 0 0 0 9 4.68a1.65 1.65 0 0 0 1-1.51V3a2 2 0 0 1 4 0v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 0 1 2.83 2.83l-.06.06A1.65 1.65 0 0 0 19.4 9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 0 1 0 4h-.09a1.65 1.65 0 0 0-1.51 1z" />
        </svg>
        <span v-if="!collapsed" class="sidebar-nav-label">Settings</span>
      </button>
    </div>
  </nav>
</template>

<style scoped>
.sidebar {
  width: var(--sidebar-width-expanded);
  background: var(--sidebar-bg);
  backdrop-filter: blur(24px) saturate(180%);
  -webkit-backdrop-filter: blur(24px) saturate(180%);
  border-right: 1px solid rgba(140, 170, 200, 0.08);
  display: flex;
  flex-direction: column;
  transition: width 0.2s ease;
  overflow: hidden;
  flex-shrink: 0;
  -webkit-app-region: drag;
  position: relative;
}
.sidebar::before {
  content: "";
  position: absolute;
  inset: 0;
  background: linear-gradient(180deg, rgba(95, 183, 232, 0.04) 0%, transparent 40%, rgba(95, 183, 232, 0.02) 100%);
  pointer-events: none;
  z-index: 0;
}
.sidebar > * {
  position: relative;
  z-index: 1;
}
.sidebar.collapsed {
  width: var(--sidebar-width-collapsed);
}

.sidebar-top {
  display: flex;
  align-items: center;
  gap: 8px;
  min-height: 108px;
  padding: 38px 12px 12px;
  border-bottom: 1px solid var(--border);
}

.sidebar-hamburger {
  background: transparent;
  border: 1px solid transparent;
  color: var(--text-secondary);
  cursor: pointer;
  padding: 4px;
  border-radius: var(--radius-sm);
  display: flex;
  align-items: center;
  justify-content: center;
  -webkit-app-region: no-drag;
  transition: color var(--transition);
  flex-shrink: 0;
}
.sidebar-hamburger:hover {
  color: var(--text-primary);
  border-color: var(--border);
  background: var(--sidebar-hover);
}

.sidebar-logo {
  display: flex;
  align-items: center;
  gap: 8px;
  overflow: hidden;
}
.sidebar-logo-icon {
  width: 26px;
  height: 26px;
  flex-shrink: 0;
}
.sidebar-logo-text {
  font-family: var(--font-logo);
  font-size: 10px;
  color: var(--accent);
  line-height: 1.4;
  white-space: nowrap;
}

.sidebar-nav {
  flex: 1;
  padding: 10px 8px;
  display: flex;
  flex-direction: column;
  gap: 2px;
  overflow-y: auto;
}

.sidebar-nav-item {
  display: flex;
  align-items: center;
  gap: 10px;
  min-height: 36px;
  padding: 8px 10px;
  border: 1px solid transparent;
  background: none;
  color: var(--text-secondary);
  border-radius: var(--radius-sm);
  cursor: pointer;
  transition: all var(--transition);
  width: 100%;
  text-align: left;
  font-size: 13px;
  -webkit-app-region: no-drag;
  white-space: nowrap;
}
.sidebar-nav-item:hover {
  background: var(--sidebar-hover);
  color: var(--text-primary);
  border-color: var(--border);
}
.sidebar-nav-item.active {
  background: var(--sidebar-active);
  color: var(--accent);
  border-color: var(--border);
}

.sidebar-nav-icon {
  flex-shrink: 0;
  width: 18px;
  height: 18px;
}
.sidebar-nav-label {
  overflow: hidden;
  text-overflow: ellipsis;
}

.sidebar-theme-toggle {
  margin-bottom: 4px;
}

.sidebar-bottom {
  padding: 8px;
  border-top: 1px solid var(--border);
  display: flex;
  flex-direction: column;
  gap: 2px;
}

.sidebar.collapsed .sidebar-top {
  justify-content: center;
  padding-left: 6px;
  padding-right: 6px;
}
.sidebar.collapsed .sidebar-logo {
  display: none;
}
.sidebar.collapsed .sidebar-nav-item {
  justify-content: center;
  padding: 8px;
  gap: 0;
}
.sidebar.collapsed .sidebar-nav-icon {
  display: block;
}

@media (max-width: 720px) {
  .sidebar {
    width: var(--sidebar-width-collapsed);
  }
  .sidebar-top {
    justify-content: center;
    padding-left: 6px;
    padding-right: 6px;
  }
  .sidebar-logo,
  .sidebar-nav-label {
    display: none;
  }
  .sidebar-nav-item {
    justify-content: center;
    padding: 8px;
    gap: 0;
  }
}
</style>
