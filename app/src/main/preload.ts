import { contextBridge, ipcRenderer } from "electron";

contextBridge.exposeInMainWorld("metalsharp", {
  request: (method: string, url: string, body?: Record<string, unknown>) =>
    ipcRenderer.invoke("backend:request", method, url, body),
});
