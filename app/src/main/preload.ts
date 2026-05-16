import { contextBridge, ipcRenderer } from "electron";

contextBridge.exposeInMainWorld("metalsharp", {
  request: (method: string, url: string, body?: Record<string, unknown>, timeoutMs?: number) =>
    ipcRenderer.invoke("backend:request", method, url, body, timeoutMs),
  isFirstLaunch: () => ipcRenderer.invoke("app:is-first-launch"),
  isMigrationMode: () => ipcRenderer.invoke("app:is-migration-mode"),
  restartAfterMigration: () => ipcRenderer.invoke("app:restart-after-migration"),
  ejectDmg: () => ipcRenderer.invoke("app:eject-dmg"),
  installDeps: (command: string) => ipcRenderer.invoke("app:install-deps", command),
  installHomebrew: () => ipcRenderer.invoke("app:install-homebrew"),
  onSteamappsChanged: (callback: () => void) => ipcRenderer.on("steamapps:changed", callback),
  openInFinder: (path: string) => ipcRenderer.invoke("app:open-in-finder", path),
  restartBackend: () => ipcRenderer.invoke("backend:restart"),
  isBackendAlive: () => ipcRenderer.invoke("backend:is-alive"),
  updaterEnsureReady: () => ipcRenderer.invoke("updater:ensure-ready"),
  updaterSpawnInstall: (dmgPath: string, backendPid: number, targetVersion: string) =>
    ipcRenderer.invoke("updater:spawn-install", dmgPath, backendPid, targetVersion),
  updaterInstallStatus: () => ipcRenderer.invoke("updater:install-status"),
  updaterClearStatus: () => ipcRenderer.invoke("updater:clear-status"),
  backendGetPid: () => ipcRenderer.invoke("backend:get-pid"),
  migrateCheck: () => ipcRenderer.invoke("migrate:check"),
  migrateStart: () => ipcRenderer.invoke("migrate:start"),
  migrateProgress: () => ipcRenderer.invoke("migrate:progress"),
  quitApp: () => ipcRenderer.send("app:quit"),
  pickExeFile: () => ipcRenderer.invoke("app:pick-exe-file"),
  pickImageFile: () => ipcRenderer.invoke("app:pick-image-file"),
});
