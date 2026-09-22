// Electron main process. Loads the built sandbox (dist/index.html) as a
// standing desktop app, or the Vite dev server when SANDBOX_DEV_URL is set
// (see package.json's electron:dev script) -- same page either way, no
// separate desktop-only code path to keep in sync with the web build.
import { app, BrowserWindow, shell } from "electron";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";

const __dirname = dirname(fileURLToPath(import.meta.url));
const devUrl = process.env.SANDBOX_DEV_URL;

function createWindow() {
  const win = new BrowserWindow({
    width: 1360,
    height: 860,
    minWidth: 720,
    minHeight: 560,
    // Matches the Night-Sky Chart's dark ground -- a light system window
    // chrome flashing white before the page paints reads as a bug, not a
    // loading state.
    backgroundColor: "#05070d",
    title: "SocialSandbox",
    icon: join(__dirname, "..", "build", "icon.png"),
    autoHideMenuBar: true,
    webPreferences: {
      contextIsolation: true,
      nodeIntegration: false,
      sandbox: true,
    },
  });

  // Every SocialLang builtin here runs client-side against a mock provider
  // (see PRODUCT.md) -- there's no reason for this window to navigate
  // anywhere else or open new windows, so both are refused rather than left
  // to Electron's insecure defaults.
  win.webContents.setWindowOpenHandler(({ url }) => {
    shell.openExternal(url);
    return { action: "deny" };
  });
  win.webContents.on("will-navigate", (event, url) => {
    if (!devUrl || !url.startsWith(devUrl)) event.preventDefault();
  });

  if (devUrl) {
    win.loadURL(devUrl);
  } else {
    win.loadFile(join(__dirname, "..", "dist", "index.html"));
  }
}

app.whenReady().then(() => {
  createWindow();
  app.on("activate", () => {
    if (BrowserWindow.getAllWindows().length === 0) createWindow();
  });
});

app.on("window-all-closed", () => {
  if (process.platform !== "darwin") app.quit();
});
