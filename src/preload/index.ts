import { contextBridge, ipcRenderer } from 'electron';
import { IPC } from '../shared/ipc';
import type {
  InitPayload,
  MenuState,
  OperatorManifest,
  PetConfig,
  SessionSnapshot,
} from '../shared/types';

type Unsubscribe = () => void;

const on = <T>(channel: string, cb: (value: T) => void): Unsubscribe => {
  const handler = (_event: unknown, value: T) => cb(value);
  ipcRenderer.on(channel, handler as any);
  return () => ipcRenderer.removeListener(channel, handler as any);
};

const api = {
  init: (): Promise<InitPayload> => ipcRenderer.invoke(IPC.petInit),
  menuState: (): Promise<MenuState> => ipcRenderer.invoke(IPC.menuState),
  saveConfig: (patch: Partial<PetConfig>): Promise<PetConfig> =>
    ipcRenderer.invoke(IPC.saveConfig, patch),
  setScale: (scale: number): Promise<number> => ipcRenderer.invoke(IPC.setScale, scale),
  resetScale: (): Promise<number> => ipcRenderer.invoke(IPC.resetScale),
  onScale: (cb: (scale: number) => void): Unsubscribe => on(IPC.scaleChanged, cb),
  dragStart: (move: boolean): void => ipcRenderer.send(IPC.dragStart, move),
  dragEnd: (): void => ipcRenderer.send(IPC.dragEnd),
  setIgnoreMouse: (ignore: boolean): void => ipcRenderer.send(IPC.ignoreMouse, ignore),
  openMenu: (): void => ipcRenderer.send(IPC.menuOpen),
  hideMenu: (): void => ipcRenderer.send(IPC.menuHide),
  menuAction: (action: { type: string; value?: unknown }): void =>
    ipcRenderer.send(IPC.menuAction, action),
  openCodex: (threadId: string | null): void => ipcRenderer.send(IPC.openCodex, threadId),
  quit: (): void => ipcRenderer.send(IPC.quit),
  installNativePet: (): void => ipcRenderer.send(IPC.menuAction, { type: 'installNativePet' }),
  onSide: (cb: (button: 'back' | 'forward') => void): Unsubscribe =>
    on(IPC.sideButton, cb),
  onApply: (cb: (action: { type: string; value?: any }) => void): Unsubscribe =>
    on(IPC.apply, cb),
  onSnapshot: (cb: (snapshot: SessionSnapshot) => void): Unsubscribe => on(IPC.snapshot, cb),
  markAllRead: (): void => ipcRenderer.send(IPC.markAllRead),
  setEarTip: (y: number): void => ipcRenderer.send(IPC.earTip, y),
  onMenuState: (cb: (state: MenuState) => void): Unsubscribe => on(IPC.menuState, cb),
  exporterInit: (cb: (payload: { operator: OperatorManifest }) => void): Unsubscribe =>
    on(IPC.exporterInit, cb),
  sendNativePetData: (dataUrl: string): void =>
    ipcRenderer.send(IPC.nativePetData, { dataUrl }),
};

contextBridge.exposeInMainWorld('petAPI', api);

export type PetAPI = typeof api;
