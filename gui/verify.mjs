// one-shot GUI verification: launch, screenshot, dump status, quit
import { _electron as electron } from 'playwright-core';
import path from 'node:path';

const APP_DIR = import.meta.dirname;
const app = await electron.launch({
  executablePath: path.join(APP_DIR, 'node_modules/electron/dist/electron'),
  args: ['--no-sandbox', APP_DIR],
  timeout: 30000,
});
const page = await app.firstWindow();
await page.waitForSelector('#chip', { timeout: 15000 });
// wait for the status readout to populate (spectractl call ~1-2s)
await page.waitForFunction(
  () => document.querySelector('#chip').textContent.length > 0 ||
        document.querySelector('#err').textContent.length > 0,
  { timeout: 20000 }
);
await new Promise(r => setTimeout(r, 500));
await page.screenshot({ path: '/tmp/claude-1000/-home-doodcom/62f873d8-1f10-45e2-a8d4-33b32869215a/scratchpad/gui.png' });
console.log('STATLINE:', await page.evaluate(() => document.querySelector('#statline').innerText));
console.log('CHIP:', await page.evaluate(() => document.querySelector('#chip').innerText));
console.log('ERR:', await page.evaluate(() => document.querySelector('#err').innerText));
await app.close();
