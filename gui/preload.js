const { contextBridge, ipcRenderer } = require('electron');

contextBridge.exposeInMainWorld('spectra', {
  status: () => ipcRenderer.invoke('spectra:status'),
  apply: (cmd) => ipcRenderer.invoke('spectra:apply', cmd),
});
