import {
  app,
  BrowserWindow,
  dialog,
  ipcMain,
  Menu,
  nativeImage,
  protocol,
  screen,
  shell,
  Tray,
} from 'electron';
import { execFile } from 'node:child_process';
import { existsSync, promises as fs } from 'node:fs';
import path from 'node:path';
import { IPC } from '../shared/ipc';
import type {
  BaseState,
  BattleState,
  Form,
  InitPayload,
  MenuState,
  OperatorManifest,
  PetConfig,
  SessionSnapshot,
} from '../shared/types';
import { codexHome, SessionMonitor } from './session/monitor';
import { getCodexUnread } from './session/readReceipts';

protocol.registerSchemesAsPrivileged([
  {
    scheme: 'petasset',
    privileges: {
      standard: true,
      secure: true,
      supportFetchAPI: true,
      corsEnabled: true,
      stream: true,
    },
  },
]);

const DEFAULT_OPERATOR = 'char_1015_aglna2';
const PET_BASE_HEIGHT = 375;
const DEFAULT_CONFIG: PetConfig = {
  operatorId: DEFAULT_OPERATOR,
  form: 'base',
  baseState: 'stand',
  battleState: 'default',
  scale: 0.56,
  position: null,
  pinnedThreadId: null,
  sessionCardVisible: true,
  alwaysOnTop: true,
  autostart: true,
  closeWithCodex: false,
  viewedThreads: {},
};

let config: PetConfig = { ...DEFAULT_CONFIG };
let operator: OperatorManifest;
let operators: Array<{ id: string; name: string }> = [];
let petWin: BrowserWindow | null = null;
let menuWin: BrowserWindow | null = null;
let cardWin: BrowserWindow | null = null;
let exporterWin: BrowserWindow | null = null;
let tray: Tray | null = null;
let monitor: SessionMonitor | null = null;
let codexRunning = false;
let userHidden = false;
let dragTimer: NodeJS.Timeout | null = null;
let dragOrigin = { x: 0, y: 0 };
let dragBounds = { x: 0, y: 0, width: 0, height: 0 };
let dragLast = { x: 0, y: 0 };
let allowPetResize = false;
let cardHideTimer: NodeJS.Timeout | null = null;
let saveTimer: NodeJS.Timeout | null = null;
let lastSnapshot: SessionSnapshot | null = null;
let rawSnapshot: SessionSnapshot | null = null;
let publishSeq = 0;
let earTipY = 0;
let cardHeight = 140;

const appRoot = path.resolve(__dirname, '../..');
const distDir = path.join(appRoot, 'dist');
const publicDir = path.join(distDir, 'public');
const rendererDir = path.join(distDir, 'renderer');
const preloadPath = path.join(distDir, 'preload/index.cjs');
const configPath = () => path.join(app.getPath('userData'), 'config.json');

function mimeFor(file: string): string {
  const ext = path.extname(file).toLowerCase();
  switch (ext) {
    case '.png':
      return 'image/png';
    case '.webp':
      return 'image/webp';
    case '.jpg':
    case '.jpeg':
      return 'image/jpeg';
    case '.json':
      return 'application/json';
    case '.atlas':
      return 'text/plain; charset=utf-8';
    case '.js':
      return 'text/javascript; charset=utf-8';
    case '.css':
      return 'text/css; charset=utf-8';
    case '.html':
      return 'text/html; charset=utf-8';
    default:
      return 'application/octet-stream';
  }
}

function registerAssetProtocol(): void {
  protocol.handle('petasset', async (request) => {
    const url = new URL(request.url);
    const rel = decodeURIComponent(url.pathname).replace(/^\/+/, '');
    const file = path.normalize(path.join(publicDir, rel));
    if (!file.startsWith(publicDir)) return new Response('forbidden', { status: 403 });
    try {
      const data = await fs.readFile(file);
      return new Response(new Uint8Array(data), {
        headers: { 'content-type': mimeFor(file), 'cache-control': 'no-cache' },
      });
    } catch {
      return new Response('not found', { status: 404 });
    }
  });
}

function assetUrl(rel: string): string {
  return `petasset://local/${rel.split(path.sep).join('/')}`;
}

async function loadConfig(): Promise<void> {
  try {
    const raw = JSON.parse(await fs.readFile(configPath(), 'utf8'));
    config = { ...DEFAULT_CONFIG, ...raw };
  } catch {
    config = { ...DEFAULT_CONFIG };
  }
}

function saveConfigSoon(): void {
  if (saveTimer) clearTimeout(saveTimer);
  saveTimer = setTimeout(() => {
    saveTimer = null;
    fs.mkdir(path.dirname(configPath()), { recursive: true })
      .then(() => fs.writeFile(configPath(), JSON.stringify(config, null, 2)))
      .catch(() => undefined);
  }, 200);
}

async function loadOperators(): Promise<void> {
  const base = path.join(publicDir, 'assets/operators');
  operators = [];
  let entries: string[] = [];
  try {
    entries = await fs.readdir(base);
  } catch {
    entries = [];
  }
  for (const id of entries) {
    try {
      const manifest = JSON.parse(
        await fs.readFile(path.join(base, id, 'pet.json'), 'utf8'),
      ) as OperatorManifest;
      operators.push({ id, name: manifest.name ?? id });
    } catch {
      // skip invalid operator folders
    }
  }
  if (!operators.length) operators = [{ id: DEFAULT_OPERATOR, name: '予愿安洁莉娜' }];
}

async function loadOperator(id: string): Promise<OperatorManifest> {
  const dir = path.join(publicDir, 'assets/operators', id);
  const manifest = JSON.parse(
    await fs.readFile(path.join(dir, 'pet.json'), 'utf8'),
  ) as OperatorManifest;
  manifest.referenceWidth ??= 600;
  const prefix = `assets/operators/${id}`;
  for (const kind of ['base', 'battle'] as Form[]) {
    const m = manifest.models[kind];
    m.skel = assetUrl(`${prefix}/${m.skel}`);
    m.atlas = assetUrl(`${prefix}/${m.atlas}`);
    m.texture = assetUrl(`${prefix}/${m.texture}`);
  }
  manifest.avatar = assetUrl(`${prefix}/${manifest.avatar}`);
  return manifest;
}

function petSize(scale: number): { width: number; height: number } {
  const height = Math.round(PET_BASE_HEIGHT * scale);
  const ratio = (operator.referenceWidth ?? 600) / (operator.referenceHeight ?? 520);
  return { width: Math.round(height * ratio), height };
}

function defaultPosition(width: number, height: number): { x: number; y: number } {
  const area = screen.getPrimaryDisplay().workArea;
  return { x: area.x + area.width - width - 40, y: area.y + area.height - height - 40 };
}

function clampToDisplay(pos: { x: number; y: number }, width: number, height: number) {
  const display = screen.getDisplayNearestPoint({ x: pos.x, y: pos.y });
  const area = display.workArea;
  return {
    x: Math.min(Math.max(pos.x, area.x - width + 80), area.x + area.width - 80),
    y: Math.min(Math.max(pos.y, area.y - height + 80), area.y + area.height - 40),
  };
}

function createPetWindow(): void {
  const size = petSize(config.scale);
  const pos = clampToDisplay(
    config.position ?? defaultPosition(size.width, size.height),
    size.width,
    size.height,
  );
  petWin = new BrowserWindow({
    width: size.width,
    height: size.height,
    x: pos.x,
    y: pos.y,
    transparent: true,
    backgroundColor: '#00000000',
    frame: false,
    resizable: false,
    hasShadow: false,
    skipTaskbar: true,
    alwaysOnTop: config.alwaysOnTop,
    show: false,
    webPreferences: {
      preload: preloadPath,
      contextIsolation: true,
      nodeIntegration: false,
      backgroundThrottling: false,
    },
  });
  petWin.setAlwaysOnTop(config.alwaysOnTop, 'screen-saver');
  petWin.on('will-resize', (event) => {
    if (!allowPetResize) {
      event.preventDefault();
    }
  });
  petWin.on('resize', () => {
    if (!petWin || allowPetResize) return;
    const b = petWin.getBounds();
    const size = petSize(config.scale);
    if (b.width !== size.width || b.height !== size.height) {
      allowPetResize = true;
      petWin.setBounds({ x: b.x, y: b.y, width: size.width, height: size.height }, false);
      allowPetResize = false;
    }
  });
  try {
    petWin.setVisibleOnAllWorkspaces(true);
  } catch {
    // not supported on every platform
  }
  petWin.loadFile(path.join(rendererDir, 'pet.html'));
  petWin.webContents.on('did-finish-load', () => {
    if (lastSnapshot) petWin?.webContents.send(IPC.snapshot, lastSnapshot);
  });
  petWin.webContents.on('will-navigate', (event) => event.preventDefault());
  petWin.webContents.setWindowOpenHandler(() => ({ action: 'deny' }));
  petWin.on('moved', () => {
    if (!petWin) return;
    const b = petWin.getBounds();
    config.position = { x: b.x, y: b.y };
    saveConfigSoon();
    positionCard();
  });
  petWin.on('closed', () => {
    petWin = null;
  });

  try {
    petWin.hookWindowMessage(0x020b, (wParam: Buffer) => {
      let w = 0;
      if (wParam.length >= 8) w = Number(wParam.readBigUInt64LE(0) & 0xffffffffn);
      else if (wParam.length >= 4) w = wParam.readUInt32LE(0);
      const button = (w >>> 16) & 0xffff;
      const kind = button === 1 ? 'back' : button === 2 ? 'forward' : null;
      if (kind) petWin?.webContents.send(IPC.sideButton, kind);
    });
  } catch {
    // hookWindowMessage is Windows-only; ignore elsewhere.
  }
}

function createMenuWindow(): void {
  if (menuWin) return;
  menuWin = new BrowserWindow({
    width: 330,
    height: 520,
    transparent: true,
    backgroundColor: '#00000000',
    frame: false,
    resizable: false,
    maximizable: false,
    minimizable: false,
    fullscreenable: false,
    skipTaskbar: true,
    alwaysOnTop: true,
    show: false,
    webPreferences: {
      preload: preloadPath,
      contextIsolation: true,
      nodeIntegration: false,
    },
  });
  menuWin.loadFile(path.join(rendererDir, 'menu.html'));
  menuWin.setAlwaysOnTop(true, 'screen-saver');
  menuWin.webContents.on('did-finish-load', () => {
    menuWin?.webContents.send(IPC.menuState, buildMenuState());
  });
  menuWin.on('blur', () => {
    setTimeout(() => {
      if (menuWin && !menuWin.isFocused()) menuWin.hide();
    }, 250);
  });
  menuWin.on('hide', () => {
    if (lastSnapshot && petWin?.isVisible()) showCard(lastSnapshot);
  });
  menuWin.on('closed', () => {
    menuWin = null;
  });
}

function createCardWindow(): void {
  if (cardWin) return;
  cardWin = new BrowserWindow({
    width: 350,
    height: 250,
    transparent: true,
    backgroundColor: '#00000000',
    frame: false,
    resizable: false,
    maximizable: false,
    minimizable: false,
    fullscreenable: false,
    skipTaskbar: true,
    alwaysOnTop: true,
    show: false,
    webPreferences: {
      preload: preloadPath,
      contextIsolation: true,
      nodeIntegration: false,
    },
  });
  cardWin.loadFile(path.join(rendererDir, 'card.html'));
  cardWin.setAlwaysOnTop(true, 'screen-saver');
  cardWin.setResizable(false);
  cardWin.setContentSize(350, cardHeight);
  cardWin.webContents.on('did-finish-load', () => {
    if (lastSnapshot) cardWin?.webContents.send(IPC.snapshot, lastSnapshot);
  });
  cardWin.on('closed', () => {
    cardWin = null;
  });
}

function positionCard(): void {
  if (!cardWin || !petWin || !cardWin.isVisible()) return;
  const pet = petWin.getBounds();
  const cardWidth = 350;
  const display = screen.getDisplayNearestPoint({ x: pet.x, y: pet.y });
  const area = display.workArea;
  let x = Math.round(pet.x + pet.width / 2 - cardWidth / 2);
  const dotTop = Math.max(0, earTipY - 5);
  let y = pet.y + Math.max(0, dotTop - 10) - cardHeight;
  if (y < area.y + 8) y = pet.y + pet.height + 8;
  x = Math.min(Math.max(x, area.x + 8), area.x + area.width - cardWidth - 8);
  y = Math.min(Math.max(y, area.y + 8), area.y + area.height - cardHeight - 8);
  cardWin.setBounds({ x, y, width: cardWidth, height: cardHeight }, false);
}

function dotFor(messages: SessionSnapshot['messages']): SessionSnapshot['dot'] {
  if (messages.some((m) => m.status === 'running' || m.status === 'waiting')) return 'blue';
  if (messages.some((m) => m.unread && (m.status === 'done' || m.status === 'failed'))) {
    return 'green';
  }
  return 'gray';
}

function decorateSnapshot(
  snap: SessionSnapshot,
  receipts: { map: Map<string, boolean>; ok: boolean },
): SessionSnapshot {
  const viewed = config.viewedThreads ?? {};
  const messages = snap.messages
    .filter((m) => m.status !== 'aborted')
    .map((m) => {
      const active = m.status === 'running' || m.status === 'waiting';
      const viewedAt = viewed[m.threadId] ?? 0;
      const codexUnread = receipts.ok
        ? (receipts.map.get(m.threadId) ?? false)
        : m.updatedAt > viewedAt;
      return {
        ...m,
        unread: active || (codexUnread && m.updatedAt > viewedAt),
      };
    })
    .filter(
      (m) =>
        m.status === 'running' ||
        m.status === 'waiting' ||
        (m.unread && (m.status === 'done' || m.status === 'failed')),
    );
  return { ...snap, messages, dot: dotFor(messages) };
}

function publishSnapshot(): void {
  if (!rawSnapshot) return;
  const seq = ++publishSeq;
  void getCodexUnread()
    .then((receipts) => {
      if (seq !== publishSeq || !rawSnapshot) return;
      const decorated = decorateSnapshot(rawSnapshot, receipts);
      lastSnapshot = decorated;
      if (petWin) petWin.webContents.send(IPC.snapshot, decorated);
      showCard(decorated);
    })
    .catch(() => undefined);
}

function showMenu(): void {
  if (!petWin || !menuWin) return;
  if (menuWin.isDestroyed()) {
    menuWin = null;
    createMenuWindow();
    return;
  }
  if (menuWin.isVisible()) {
    menuWin.hide();
    return;
  }
  cardWin?.hide();
  menuWin.webContents.send(IPC.menuState, buildMenuState());
  const pet = petWin.getBounds();
  const size = menuWin.getBounds();
  const display = screen.getDisplayNearestPoint({ x: pet.x, y: pet.y });
  const area = display.workArea;
  let x = pet.x - size.width - 10;
  if (x < area.x + 8) x = pet.x + pet.width + 10;
  x = Math.min(Math.max(x, area.x + 8), area.x + area.width - size.width - 8);
  const y = Math.min(Math.max(pet.y + pet.height - size.height, area.y + 8), area.y + area.height - size.height - 8);
  menuWin.setPosition(Math.round(x), Math.round(y), false);
  menuWin.show();
  menuWin.focus();
  menuWin.moveTop();
}

function buildMenuState(): MenuState {
  return {
    operatorId: config.operatorId,
    operators,
    form: config.form,
    baseState: config.baseState,
    battleState: config.battleState,
    scale: config.scale,
    pinnedThreadId: config.pinnedThreadId,
    recentThreads: monitor?.getRecentThreads() ?? [],
    sessionCardVisible: config.sessionCardVisible,
    alwaysOnTop: config.alwaysOnTop,
    autostart: config.autostart,
    closeWithCodex: config.closeWithCodex,
  };
}

function applyScale(scale: number, anchor?: { x: number; y: number }): void {
  if (!petWin) return;
  const next = Math.min(2, Math.max(0.4, Number(scale.toFixed(3))));
  const old = petWin.getBounds();
  const size = petSize(next);
  const cursor = anchor ?? screen.getCursorScreenPoint();
  const inside =
    cursor.x >= old.x &&
    cursor.x <= old.x + old.width &&
    cursor.y >= old.y &&
    cursor.y <= old.y + old.height;
  let x: number;
  let y: number;
  if (inside) {
    const ux = (cursor.x - old.x) / Math.max(1, old.width);
    const uy = (cursor.y - old.y) / Math.max(1, old.height);
    x = Math.round(cursor.x - ux * size.width);
    y = Math.round(cursor.y - uy * size.height);
  } else {
    x = Math.round(old.x + (old.width - size.width) / 2);
    y = Math.round(old.y + (old.height - size.height));
  }
  const clamped = clampToDisplay({ x, y }, size.width, size.height);
  x = clamped.x;
  y = clamped.y;
  config.scale = next;
  allowPetResize = true;
  petWin.setBounds({ x, y, width: size.width, height: size.height });
  allowPetResize = false;
  petWin.webContents.send(IPC.scaleChanged, next);
  config.position = { x, y };
  if (dragTimer) {
    dragBounds = { x, y, width: size.width, height: size.height };
    dragOrigin = screen.getCursorScreenPoint();
    dragLast = { x, y };
  }
  saveConfigSoon();
  positionCard();
}

function startDrag(move: boolean): void {
  endDrag();
  dragOrigin = screen.getCursorScreenPoint();
  if (!petWin) return;
  const b = petWin.getBounds();
  dragBounds = { x: b.x, y: b.y, width: b.width, height: b.height };
  dragLast = { x: b.x, y: b.y };
  if (!move) return;
  dragTimer = setInterval(() => {
    if (!petWin) return;
    const p = screen.getCursorScreenPoint();
    const dx = p.x - dragOrigin.x;
    const dy = p.y - dragOrigin.y;
    if (Math.abs(dx) < 1 && Math.abs(dy) < 1) return;
    const nx = Math.round(dragBounds.x + dx);
    const ny = Math.round(dragBounds.y + dy);
    if (nx === dragLast.x && ny === dragLast.y) return;
    dragLast = { x: nx, y: ny };
    const size = petSize(config.scale);
    allowPetResize = true;
    petWin.setBounds({ x: nx, y: ny, width: size.width, height: size.height }, false);
    allowPetResize = false;
    positionCard();
  }, 16);
}

function endDrag(): void {
  if (dragTimer) clearInterval(dragTimer);
  dragTimer = null;
  if (petWin) {
    const b = petWin.getBounds();
    config.position = { x: b.x, y: b.y };
    saveConfigSoon();
    positionCard();
  }
}

function showCard(snap: SessionSnapshot): void {
  if (!cardWin) createCardWindow();
  if (!cardWin) return;
  if (cardHideTimer) clearTimeout(cardHideTimer);
  cardHideTimer = null;
  if (!config.sessionCardVisible) {
    cardWin.hide();
    return;
  }
  if (!snap.messages.length) {
    cardWin.hide();
    return;
  }
  cardHeight = Math.min(440, Math.max(140, 60 + snap.messages.length * 42 + 36));
  cardWin.webContents.send(IPC.snapshot, snap);
  cardWin.showInactive();
  cardWin.moveTop();
  positionCard();
}

function updateVisibility(): void {
  if (!petWin) return;
  if (codexRunning && !userHidden) {
    petWin.showInactive();
    petWin.moveTop();
    if (lastSnapshot) showCard(lastSnapshot);
  } else {
    petWin.hide();
    menuWin?.hide();
    cardWin?.hide();
  }
}

function checkCodex(): void {
  execFile(
    'tasklist.exe',
    ['/FI', 'IMAGENAME eq ChatGPT.exe', '/NH'],
    { windowsHide: true },
    (err, stdout) => {
      const running = !err && /chatgpt\.exe/i.test(stdout);
      if (running === codexRunning) return;
      codexRunning = running;
      if (!codexRunning) {
        endDrag();
        if (config.closeWithCodex) {
          app.quit();
          return;
        }
      }
      updateVisibility();
      refreshTray();
    },
  );
}

function openCodex(threadId?: string | null): void {
  const message = threadId
    ? lastSnapshot?.messages.find((m) => m.threadId === threadId)
    : undefined;
  if (threadId && message && (message.status === 'done' || message.status === 'failed')) {
    config.viewedThreads = config.viewedThreads ?? {};
    config.viewedThreads[threadId] = Date.now();
    saveConfigSoon();
    if (lastSnapshot) {
      const messages = lastSnapshot.messages.filter((m) => m.threadId !== threadId);
      lastSnapshot = { ...lastSnapshot, messages, dot: dotFor(messages) };
      showCard(lastSnapshot);
    }
  }
  const url = threadId ? `codex://threads/${encodeURIComponent(threadId)}` : 'codex://launch';
  shell.openExternal(url).catch(() => {
    execFile(
      'explorer.exe',
      ['shell:AppsFolder\\OpenAI.Codex_2p2nqsd0c76g0!App'],
      { windowsHide: true },
      () => undefined,
    );
  });
}

function trayIcon() {
  const avatarPath = path.join(publicDir, 'assets/operators', config.operatorId, 'avatar.png');
  const img = existsSync(avatarPath) ? nativeImage.createFromPath(avatarPath) : nativeImage.createEmpty();
  return img.isEmpty() ? img : img.resize({ width: 16, height: 16 });
}

function refreshTray(): void {
  if (!tray) return;
  tray.setImage(trayIcon());
  tray.setToolTip(`予愿安洁莉娜 · Codex Pet${codexRunning ? '' : ' (Codex 未运行)'}`);
  const menu = Menu.buildFromTemplate([
    {
      label: userHidden ? '显示宠物' : '隐藏宠物',
      click: () => {
        userHidden = !userHidden;
        updateVisibility();
        refreshTray();
      },
    },
    { label: '打开 Codex', click: () => openCodex(lastSnapshot?.threadId ?? null) },
    { type: 'separator' },
    {
      label: '随 Windows 启动',
      type: 'checkbox',
      checked: config.autostart,
      click: (item) => {
        config.autostart = item.checked;
        saveConfigSoon();
        if (app.isPackaged) {
          app.setLoginItemSettings({ openAtLogin: config.autostart, path: process.execPath });
        }
      },
    },
    {
      label: 'Codex 退出后关闭本程序',
      type: 'checkbox',
      checked: config.closeWithCodex,
      click: (item) => {
        config.closeWithCodex = item.checked;
        saveConfigSoon();
      },
    },
    { type: 'separator' },
    { label: '退出', click: () => app.quit() },
  ]);
  tray.setContextMenu(menu);
}

function createTray(): void {
  tray = new Tray(trayIcon());
  tray.on('click', () => {
    userHidden = !userHidden;
    updateVisibility();
    refreshTray();
  });
  refreshTray();
}

async function runNativeExport(): Promise<{ installDir: string; sheetPath: string }> {
  return new Promise(async (resolve, reject) => {
    const win = new BrowserWindow({
      width: 900,
      height: 700,
      show: false,
      webPreferences: {
        preload: preloadPath,
        contextIsolation: true,
        nodeIntegration: false,
        offscreen: false,
      },
    });
    exporterWin = win;
    const timeout = setTimeout(() => {
      cleanup();
      reject(new Error('导出超时'));
    }, 30000);
    const onData = async (_event: Electron.IpcMainEvent, payload: { dataUrl: string }) => {
      try {
        cleanup();
        const match = /^data:image\/(png|webp);base64,(.+)$/.exec(payload.dataUrl);
        if (!match) throw new Error('导出数据格式不正确');
        const bytes = Buffer.from(match[2], 'base64');
        const ext = match[1];
        const outDir = path.join(app.getPath('userData'), 'codex-pet');
        await fs.mkdir(outDir, { recursive: true });
        const sheetPath = path.join(outDir, `spritesheet.${ext}`);
        await fs.writeFile(sheetPath, bytes);
        const installDir = path.join(codexHome(), 'pets', 'yu-yuan-anjielina');
        await fs.mkdir(installDir, { recursive: true });
        await fs.writeFile(path.join(installDir, `spritesheet.${ext}`), bytes);
        await fs.writeFile(
          path.join(installDir, 'pet.json'),
          JSON.stringify(
            {
              displayName: operator.name,
              description: '明日方舟 · 予愿安洁莉娜',
              spriteVersionNumber: 2,
              spritesheetPath: `spritesheet.${ext}`,
            },
            null,
            2,
          ),
        );
        resolve({ installDir, sheetPath });
      } catch (err) {
        reject(err);
      }
    };
    const cleanup = () => {
      clearTimeout(timeout);
      ipcMain.removeListener(IPC.nativePetData, onData as any);
      if (exporterWin) {
        exporterWin.destroy();
        exporterWin = null;
      }
    };
    ipcMain.on(IPC.nativePetData, onData as any);
    win.webContents.on('did-finish-load', () => {
      win.webContents.send(IPC.exporterInit, { operator });
    });
    win.loadFile(path.join(rendererDir, 'exporter.html'));
  });
}

function registerIpc(): void {
  ipcMain.handle(IPC.petInit, async (): Promise<InitPayload> => ({
    config,
    operator,
    operators,
  }));
  ipcMain.handle(IPC.menuState, async () => buildMenuState());
  ipcMain.handle(IPC.saveConfig, async (_e, patch: Partial<PetConfig>) => {
    Object.assign(config, patch);
    saveConfigSoon();
    return config;
  });
  ipcMain.handle(IPC.setScale, async (_e, scale: number) => {
    applyScale(scale, screen.getCursorScreenPoint());
    return config.scale;
  });
  ipcMain.handle(IPC.resetScale, async () => {
    applyScale(DEFAULT_CONFIG.scale, screen.getCursorScreenPoint());
    return config.scale;
  });
  ipcMain.on(IPC.dragStart, (_e, move: boolean) => startDrag(Boolean(move)));
  ipcMain.on(IPC.dragEnd, () => endDrag());
  ipcMain.on(IPC.ignoreMouse, (_e, ignore: boolean) => {
    if (!petWin) return;
    if (ignore) petWin.setIgnoreMouseEvents(true, { forward: true });
    else petWin.setIgnoreMouseEvents(false);
  });
  ipcMain.on(IPC.menuOpen, () => showMenu());
  ipcMain.on(IPC.menuHide, () => menuWin?.hide());
  ipcMain.on(IPC.openCodex, (_e, threadId: string | null) => openCodex(threadId));
  ipcMain.on(IPC.earTip, (_e, y: number) => {
    if (typeof y === 'number' && Number.isFinite(y)) {
      earTipY = y;
      positionCard();
    }
  });
  ipcMain.on(IPC.markAllRead, () => {
    if (!lastSnapshot) return;
    config.viewedThreads = config.viewedThreads ?? {};
    const now = Date.now();
    const messages = lastSnapshot.messages.filter((m) => {
      if (m.status === 'done' || m.status === 'failed') {
        config.viewedThreads[m.threadId] = now;
        return false;
      }
      return true;
    });
    saveConfigSoon();
    lastSnapshot = { ...lastSnapshot, messages, dot: dotFor(messages) };
    if (messages.length) showCard(lastSnapshot);
    else cardWin?.hide();
  });
  ipcMain.on(IPC.quit, () => app.quit());
  ipcMain.on(IPC.showPet, () => {
    userHidden = false;
    updateVisibility();
    refreshTray();
  });
  ipcMain.on(IPC.hidePet, () => {
    userHidden = true;
    updateVisibility();
    refreshTray();
  });

  ipcMain.on(IPC.menuAction, async (_e, action: { type: string; value?: any }) => {
    menuWin?.hide();
    switch (action.type) {
      case 'form':
      case 'baseState':
      case 'battleState':
        petWin?.webContents.send(IPC.apply, action);
        break;
      case 'operator': {
        const id = String(action.value);
        config.operatorId = id;
        saveConfigSoon();
        operator = await loadOperator(id);
        petWin?.webContents.send(IPC.apply, {
          type: 'reload',
          value: { config, operator, operators },
        });
        refreshTray();
        break;
      }
      case 'scale':
        applyScale(Number(action.value));
        break;
      case 'resetScale':
        applyScale(DEFAULT_CONFIG.scale, screen.getCursorScreenPoint());
        break;
      case 'pinThread':
        config.pinnedThreadId = action.value ?? null;
        saveConfigSoon();
        monitor?.setPinned(config.pinnedThreadId);
        break;
      case 'toggleCard':
        config.sessionCardVisible = !config.sessionCardVisible;
        saveConfigSoon();
        if (config.sessionCardVisible && lastSnapshot) showCard(lastSnapshot);
        else cardWin?.hide();
        break;
      case 'toggleTop':
        config.alwaysOnTop = !config.alwaysOnTop;
        petWin?.setAlwaysOnTop(config.alwaysOnTop, 'screen-saver');
        saveConfigSoon();
        break;
      case 'toggleAutostart':
        config.autostart = !config.autostart;
        saveConfigSoon();
        if (app.isPackaged) {
          app.setLoginItemSettings({ openAtLogin: config.autostart, path: process.execPath });
        }
        refreshTray();
        break;
      case 'toggleCloseWithCodex':
        config.closeWithCodex = !config.closeWithCodex;
        saveConfigSoon();
        refreshTray();
        break;
      case 'installNativePet': {
        try {
          const result = await runNativeExport();
          await dialog.showMessageBox({
            type: 'info',
            title: 'Codex 原生宠物已安装',
            message: '予愿安洁莉娜已装入 Codex 宠物目录',
            detail: `位置：${result.installDir}\n\n打开 Codex 设置 → 个性化 → Mini & Pets，点一次 Refresh，然后选择“予愿安洁莉娜”。`,
          });
        } catch (err) {
          await dialog.showMessageBox({
            type: 'error',
            title: '安装失败',
            message: String(err instanceof Error ? err.message : err),
          });
        }
        break;
      }
      case 'quit':
        app.quit();
        break;
      default:
        break;
    }
    if (menuWin) menuWin.webContents.send(IPC.menuState, buildMenuState());
  });
}

async function bootstrap(): Promise<void> {
  registerAssetProtocol();
  await loadConfig();
  await loadOperators();
  operator = await loadOperator(config.operatorId);
  registerIpc();
  createPetWindow();
  createMenuWindow();
  createCardWindow();
  createTray();

  monitor = new SessionMonitor();
  monitor.setPinned(config.pinnedThreadId);
  monitor.on('snapshot', (snap: SessionSnapshot) => {
    rawSnapshot = snap;
    publishSnapshot();
  });
  monitor.start();

  // Read receipts change without producing a rollout event. Re-evaluate the
  // unread/green/blue/gray state independently of session-log changes.
  setInterval(publishSnapshot, 1500);

  checkCodex();
  setInterval(checkCodex, 2500);

  if (app.isPackaged) {
    app.setLoginItemSettings({ openAtLogin: config.autostart, path: process.execPath });
  }

  if (process.argv.includes('--export-native-pet')) {
    try {
      const result = await runNativeExport();
      console.log(`Native Codex pet installed to ${result.installDir}`);
    } catch (err) {
      console.error(err);
    }
    app.quit();
  }

  if (process.argv.includes('--capture-pet')) {
    setTimeout(async () => {
      try {
        const img = await petWin?.webContents.capturePage();
        if (img) {
          const out = path.join(app.getPath('temp'), 'pet-window.png');
          await fs.writeFile(out, img.toPNG());
          console.log(`captured ${out}`);
        }
      } finally {
        app.quit();
      }
    }, 2500);
  }
}

const gotLock = app.requestSingleInstanceLock();
if (!gotLock) {
  app.quit();
} else {
  app.on('second-instance', () => {
    userHidden = false;
    updateVisibility();
    petWin?.focus();
  });
  app.whenReady().then(bootstrap).catch((err) => {
    console.error(err);
    app.quit();
  });
}

app.on('window-all-closed', () => {
  // Keep running in the tray so the pet can reappear with Codex.
});
