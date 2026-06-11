<script setup lang="ts">
import { ref, type Component } from "vue";
import IconMenu from "~icons/lucide/menu";
import IconServer from "~icons/lucide/server";
import IconLayers from "~icons/lucide/layers";
import IconFileText from "~icons/lucide/file-text";
import IconMoon from "~icons/lucide/moon";
import IconSun from "~icons/lucide/sun";
import IconSettings from "~icons/lucide/settings";

defineProps<{
  currentView: string;
  theme: "dark" | "light";
}>();

const emit = defineEmits<{
  navigate: [view: string];
  toggleTheme: [];
}>();

const collapsed = ref(false);

interface NavItem {
  view: string;
  label: string;
  icon: Component;
}

const navItems: NavItem[] = [
  { view: "library", label: "Library", icon: IconServer },
  { view: "sharp-library", label: "Sharp", icon: IconLayers },
  { view: "logs", label: "Logs", icon: IconFileText },
];
</script>

<template>
  <nav class="sidebar" :class="{ collapsed }">
    <div class="sidebar-top">
      <button class="sidebar-hamburger" @click="collapsed = !collapsed" title="Toggle sidebar">
        <IconMenu width="18" height="18" />
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
        <component :is="item.icon" class="sidebar-nav-icon" width="18" height="18" />
        <span v-if="!collapsed" class="sidebar-nav-label">{{ item.label }}</span>
      </button>
    </div>

    <div class="sidebar-bottom">
      <button
        class="sidebar-nav-item sidebar-theme-toggle"
        @click="emit('toggleTheme')"
        :title="collapsed ? (theme === 'dark' ? 'Light Mode' : 'Dark Mode') : undefined"
      >
        <IconMoon v-if="theme === 'dark'" class="sidebar-nav-icon" width="18" height="18" />
        <IconSun v-else class="sidebar-nav-icon" width="18" height="18" />
        <span v-if="!collapsed" class="sidebar-nav-label">{{ theme === "dark" ? "Light Mode" : "Dark Mode" }}</span>
      </button>
      <button
        class="sidebar-nav-item"
        :class="{ active: currentView === 'settings' }"
        @click="emit('navigate', 'settings')"
        :title="collapsed ? 'Settings' : undefined"
      >
        <IconSettings class="sidebar-nav-icon" width="18" height="18" />
        <span v-if="!collapsed" class="sidebar-nav-label">Settings</span>
      </button>
    </div>
  </nav>
</template>

<style scoped>
.sidebar {
  width: var(--sidebar-width-expanded);
  background: var(--sidebar-bg);
  backdrop-filter: blur(28px) saturate(200%);
  -webkit-backdrop-filter: blur(28px) saturate(200%);
  border-right: 1px solid rgba(140, 170, 200, 0.1);
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
  background: linear-gradient(180deg, rgba(30, 58, 82, 0.18) 0%, transparent 40%, rgba(26, 45, 66, 0.12) 100%);
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
  color: var(--sidebar-text);
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
  color: var(--sidebar-text-hover);
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
  color: transparent;
  background: linear-gradient(90deg, var(--sidebar-logo-color), var(--sidebar-logo-accent), var(--sidebar-logo-color), var(--sidebar-logo-accent));
  background-size: 300% 100%;
  -webkit-background-clip: text;
  background-clip: text;
  line-height: 1.4;
  white-space: nowrap;
  animation: logo-shift 6s linear infinite;
}
@keyframes logo-shift {
  0% { background-position: 0% 50%; }
  100% { background-position: 100% 50%; }
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
  justify-content: center;
  gap: 10px;
  min-height: 36px;
  padding: 8px 10px;
  border: 1px solid transparent;
  background: none;
  color: var(--sidebar-text);
  font-weight: 700;
  border-radius: var(--radius-sm);
  cursor: pointer;
  transition: all var(--transition);
  width: 100%;
  text-align: center;
  font-size: 13px;
  -webkit-app-region: no-drag;
  white-space: nowrap;
}
.sidebar-nav-item:hover {
  background: var(--sidebar-hover);
  color: var(--sidebar-text-hover);
  border-color: var(--border);
}
.sidebar-nav-item.active {
  background: var(--sidebar-active);
  color: var(--sidebar-text-active);
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
