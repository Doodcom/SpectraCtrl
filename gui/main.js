const { app, BrowserWindow, ipcMain } = require('electron');
const { execFile } = require('child_process');
const path = require('path');
const fs = require('fs');
const os = require('os');

function findSpectractl() {
  const candidates = [
    path.join(os.homedir(), '.local/bin/spectractl'),
    '/usr/local/bin/spectractl',
    '/usr/bin/spectractl',
    path.join(__dirname, '..', 'spectractl'),
  ];
  for (const c of candidates) {
    if (fs.existsSync(c)) return c;
  }
  return 'spectractl'; // hope it's on PATH
}

const BIN = findSpectractl();

// KDE Wayland + Vulkan clashes with Electron's Wayland ozone backend
// (blank window / no launch). XWayland is always available — use it.
// GPU acceleration buys nothing for a small control panel and its
// sandbox trips over the split Mesa GBM loader — skip it entirely.
if (process.platform === 'linux') {
  app.commandLine.appendSwitch('ozone-platform', 'x11');
  app.disableHardwareAcceleration();
}

function run(args) {
  return new Promise((resolve) => {
    execFile(BIN, args, { timeout: 20000 }, (err, stdout, stderr) => {
      resolve({ ok: !err, stdout: stdout || '', stderr: stderr || '' });
    });
  });
}

function parseStatus(text) {
  const get = (key) => {
    const m = text.match(new RegExp(`^${key}:\\s*(.+)$`, 'm'));
    return m ? m[1].trim() : null;
  };
  return {
    controller: get('controller'),
    power: get('power'),
    mode: get('mode'),
    color: get('color'),
    timing: get('timing'),
  };
}

ipcMain.handle('spectra:status', async () => {
  const r = await run(['status']);
  return r.ok ? { ok: true, ...parseStatus(r.stdout) }
              : { ok: false, error: r.stderr || r.stdout };
});

ipcMain.handle('spectra:apply', async (_e, cmd) => {
  // cmd: { kind: 'color'|'mode'|'on'|'off', color?, mode?, speed? }
  let args;
  switch (cmd.kind) {
    case 'color': args = ['color', cmd.color]; break;
    case 'mode':
      args = ['mode', cmd.mode];
      if (cmd.color) args.push(cmd.color);
      if (cmd.speed) args.push(String(cmd.speed));
      break;
    case 'on': args = ['on']; break;
    case 'off': args = ['off']; break;
    default: return { ok: false, error: 'bad command' };
  }
  const r = await run(args);
  return r.ok ? { ok: true } : { ok: false, error: r.stderr || r.stdout };
});

app.whenReady().then(() => {
  const win = new BrowserWindow({
    width: 440,
    height: 640,
    resizable: false,
    autoHideMenuBar: true,
    backgroundColor: '#101014',
    webPreferences: {
      preload: path.join(__dirname, 'preload.js'),
      contextIsolation: true,
      nodeIntegration: false,
    },
  });
  win.loadFile('index.html');
});

app.on('window-all-closed', () => app.quit());
