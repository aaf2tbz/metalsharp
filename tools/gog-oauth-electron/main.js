// GOG OAuth helper — a minimal Electron browser with a Chrome-style URL bar
// that shows the current URL at all times. Uses a <webview> tag so the OAuth
// flow (including Steam/Google popups) stays contained and window.opener works.
//
// The authorization code is printed to stdout on success.
const { app, BrowserWindow, ipcMain } = require("electron");
const path = require("path");

const authUrl = process.argv[2];
if (!authUrl) {
  console.error("Usage: electron main.js <gog-auth-url>");
  app.exit(1);
}

let parsed;
try { parsed = new URL(authUrl); } catch {
  console.error("Invalid URL");
  app.exit(1);
}
if (parsed.hostname !== "auth.gog.com") {
  console.error("Refusing to open non-GOG auth URL");
  app.exit(1);
}

let settled = false;

function finish(code) {
  if (settled) return;
  settled = true;
  if (code) {
    process.stdout.write(code);
  }
  app.quit();
}

app.whenReady().then(() => {
  const win = new BrowserWindow({
    width: 1024,
    height: 780,
    title: "Sign in to GOG",
    titleBarStyle: "default",
    webPreferences: {
      webviewTag: true,
      nodeIntegration: true,
      contextIsolation: false,
    },
  });

  // Renderer sends the authorization code (or error) via IPC.
  ipcMain.on("oauth-result", (_e, result) => {
    if (result.code) {
      finish(result.code);
    } else if (result.error) {
      console.error(result.error);
      finish(null);
    }
  });

  win.on("closed", () => finish(null));

  // Load the UI, then send the auth URL once the page is ready.
  const htmlPath = path.join(__dirname, "index.html");
  // Pass the auth URL as a query param so the renderer can read it immediately.
  const urlWithQuery = `file://${htmlPath}?auth=${encodeURIComponent(authUrl)}`;
  win.loadURL(urlWithQuery).catch(err => {
    console.error("Failed to load UI:", err.message);
    finish(null);
  });
});
