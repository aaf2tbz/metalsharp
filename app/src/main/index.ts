import { app, BrowserWindow, ipcMain } from "electron";
import * as path from "path";
import { RustBridge } from "./rust-bridge";

let mainWindow: BrowserWindow | null = null;
let bridge: RustBridge;

async function createWindow() {
  mainWindow = new BrowserWindow({
    width: 1200,
    height: 800,
    minWidth: 900,
    minHeight: 600,
    title: "MetalSharp",
    backgroundColor: "#1a1118",
    titleBarStyle: "hiddenInset",
    trafficLightPosition: { x: 16, y: 16 },
    webPreferences: {
      preload: path.join(__dirname, "preload.js"),
      contextIsolation: true,
      nodeIntegration: false,
    },
  });

  mainWindow.loadFile(path.join(__dirname, "..", "renderer", "index.html"));
}

app.whenReady().then(async () => {
  bridge = new RustBridge();
  await bridge.start();

  registerIpc();

  await createWindow();

  app.on("activate", () => {
    if (BrowserWindow.getAllWindows().length === 0) createWindow();
  });
});

app.on("window-all-closed", () => {
  bridge?.stop();
  if (process.platform !== "darwin") app.quit();
});

function registerIpc() {
  ipcMain.handle("backend:request", async (_e, method: string, url: string, body?: Record<string, unknown>) => {
    return bridge.request(method, url, body);
  });
}
