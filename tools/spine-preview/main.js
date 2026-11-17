const { app, BrowserWindow, ipcMain } = require('electron');
const fs = require('node:fs');
const path = require('node:path');

const args = process.argv.slice(2).filter((a) => a !== '--');
const options = {
  anim: args[0] || 'Relax',
  time: Number(args[1] || 0),
  out: args[2] || path.join(process.env.TEMP || '.', 'spine-frame.png'),
  form: args[3] || 'base',
  scale: Number(args[4] || 1.6),
  width: Number(args[5] || 610),
  height: Number(args[6] || 602),
  only: args[7] || '',
  hide: args[8] || '',
};

const root = path.resolve(__dirname, '../..');
const operatorDir = path.join(root, 'public/assets/operators/char_1015_aglna2');
const manifest = JSON.parse(fs.readFileSync(path.join(operatorDir, 'pet.json'), 'utf8'));
const model = manifest.models[options.form] || manifest.models.base;
const atlasPath = path.join(operatorDir, model.atlas);
const skelPath = path.join(operatorDir, model.skel);

app.commandLine.appendSwitch('force-device-scale-factor', '1.5');

app.whenReady().then(async () => {
  const win = new BrowserWindow({
    width: options.width,
    height: options.height,
    show: false,
    webPreferences: {
      nodeIntegration: true,
      contextIsolation: false,
      offscreen: false,
    },
  });
  win.webContents.on('console-message', (_e, _level, message) => console.log('[page]', message));
  await win.loadFile(path.join(__dirname, 'index.html'));
  win.webContents.on('render-process-gone', (_e, details) => console.error('renderer gone', details));
  const payload = {
    atlas: fs.readFileSync(atlasPath, 'utf8'),
    skel: Array.from(fs.readFileSync(skelPath)),
    images: {},
    anim: options.anim,
    time: options.time,
    referenceHeight: manifest.referenceHeight,
    referenceWidth: manifest.referenceWidth,
    only: options.only,
    hide: options.hide,
  };
  const lines = payload.atlas.split(/\r?\n/);
  const pageNames = [];
  for (let i = 0; i < lines.length; i++) {
    const line = lines[i];
    if (!line.trim() || /^\s/.test(line) || line.includes(':')) continue;
    const next = lines.slice(i + 1).find((l) => l.trim());
    if (next && /^\s*size\s*:/i.test(next)) pageNames.push(line.trim());
  }
  console.log('pages:', pageNames.join(','));
  for (const page of pageNames) {
    payload.images[page] = fs.readFileSync(path.join(path.dirname(atlasPath), page)).toString('base64');
  }
  ipcMain.once('frame', async (_event, dataUrl) => {
    const match = /^data:image\/png;base64,(.+)$/.exec(dataUrl);
    if (match) fs.writeFileSync(options.out, Buffer.from(match[1], 'base64'));
    app.quit();
  });
  try {
    await win.webContents.executeJavaScript(`renderFrame(${JSON.stringify(payload)})`);
  } catch (err) {
    console.error('render failed:', err);
    app.exit(1);
  }
});
