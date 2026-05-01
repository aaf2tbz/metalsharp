import { contextBridge, ipcRenderer } from "electron";

contextBridge.exposeInMainWorld("metalsharp", {
  request: (method: string, url: string, body?: Record<string, unknown>) =>
    ipcRenderer.invoke("backend:request", method, url, body),
  isFirstLaunch: () =>
    ipcRenderer.invoke("app:is-first-launch"),
  ejectDmg: () =>
    ipcRenderer.invoke("app:eject-dmg"),
  installDeps: (command: string) =>
    ipcRenderer.invoke("app:install-deps", command),
});
