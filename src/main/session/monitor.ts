import { EventEmitter } from 'node:events';
import { promises as fs } from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import type { RecentThread, SessionMessage, SessionSnapshot } from '../../shared/types';
import { createRolloutState, parseRolloutText, type RolloutState } from './rollout';

const UUID_RE = /[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}/gi;
const INITIAL_TAIL_BYTES = 384 * 1024;
const MAX_THREADS = 12;

export function codexHome(): string {
  return process.env.CODEX_HOME || path.join(os.homedir(), '.codex');
}

interface IndexEntry extends RecentThread {}

interface ThreadRuntime {
  path: string;
  offset: number;
  remainder: string;
  state: RolloutState;
}

export class SessionMonitor extends EventEmitter {
  private recent: IndexEntry[] = [];
  private rolloutById = new Map<string, string>();
  private subagentIds = new Set<string>();
  private runtimes = new Map<string, ThreadRuntime>();
  private messages: SessionMessage[] = [];
  private pinned: string | null = null;
  private timer: NodeJS.Timeout | null = null;
  private lastEmit = '';
  private scanAt = 0;
  private lastSizes = new Map<string, number>();
  private activeAt = new Map<string, number>();

  start(): void {
    if (this.timer) return;
    this.timer = setInterval(() => {
      this.tick().catch(() => undefined);
    }, 700);
    this.tick().catch(() => undefined);
  }

  stop(): void {
    if (this.timer) clearInterval(this.timer);
    this.timer = null;
  }

  setPinned(id: string | null): void {
    this.pinned = id;
    this.tick().catch(() => undefined);
  }

  getPinned(): string | null {
    return this.pinned;
  }

  getRecentThreads(): RecentThread[] {
    return this.recent.slice(0, MAX_THREADS);
  }

  getSnapshot(): SessionSnapshot {
    const currentId =
      this.pinned ?? this.messages[0]?.threadId ?? this.recent[0]?.id ?? null;
    const current =
      this.messages.find((m) => m.threadId === currentId) ??
      ({
        threadId: currentId,
        title: this.recent.find((r) => r.id === currentId)?.title ?? 'Codex 会话',
        status: 'idle',
        planSteps: [],
        pendingQuestion: null,
        lastAgentMessage: null,
        updatedAt: 0,
        unread: false,
      } as SessionMessage);
    return {
      threadId: current.threadId,
      title: current.title,
      status: current.status,
      planSteps: current.planSteps,
      pendingQuestion: current.pendingQuestion,
      lastAgentMessage: current.lastAgentMessage,
      updatedAt: current.updatedAt,
      messages: this.messages,
      dot: 'gray',
    };
  }

  private async tick(): Promise<void> {
    await this.refreshIndex();
    for (const entry of this.recent.slice(0, MAX_THREADS)) {
      await this.ensureRuntime(entry.id);
    }
    await Promise.all(
      this.recent
        .slice(0, MAX_THREADS)
        .map((entry) => this.tailRuntime(entry.id)),
    );
    this.messages = this.buildMessages();
    this.emitSnapshot();
  }

  private buildMessages(): SessionMessage[] {
    const list: SessionMessage[] = [];
    const startOfToday = new Date();
    startOfToday.setHours(0, 0, 0, 0);
    const cutoff = startOfToday.getTime();
    for (const entry of this.recent) {
      const rt = this.runtimes.get(entry.id);
      if (!rt) continue;
      if (rt.state.status === 'idle') continue;
      const updatedAt = Math.max(
        entry.updatedAt,
        rt.state.updatedAt,
        this.activeAt.get(entry.id) ?? 0,
      );
      if (updatedAt < cutoff) continue;
      list.push({
        threadId: entry.id,
        title: entry.title || entry.id,
        status: rt.state.status,
        planSteps: rt.state.planSteps,
        pendingQuestion: rt.state.pendingQuestion,
        lastAgentMessage: rt.state.lastAgentMessage,
        updatedAt,
        unread: false,
      });
    }
    return list.sort((a, b) => b.updatedAt - a.updatedAt).slice(0, MAX_THREADS);
  }

  private async ensureRuntime(id: string): Promise<void> {
    if (this.runtimes.has(id)) return;
    const rollout = await this.findRollout(id);
    if (!rollout) return;
    this.runtimes.set(id, {
      path: rollout,
      offset: 0,
      remainder: '',
      state: createRolloutState(),
    });
  }

  private async tailRuntime(id: string): Promise<void> {
    const rt = this.runtimes.get(id);
    if (!rt) return;
    let stat;
    try {
      stat = await fs.stat(rt.path);
    } catch {
      return;
    }
    if (rt.offset === 0) {
      const start = Math.max(0, stat.size - INITIAL_TAIL_BYTES);
      const length = stat.size - start;
      if (length <= 0) return;
      const fh = await fs.open(rt.path, 'r');
      try {
        const buf = Buffer.alloc(length);
        await fh.read(buf, 0, length, start);
        let text = buf.toString('utf8');
        if (start > 0) {
          const nl = text.indexOf('\n');
          text = nl >= 0 ? text.slice(nl + 1) : '';
        }
        rt.remainder = parseRolloutText(rt.state, text);
        rt.offset = stat.size;
      } finally {
        await fh.close();
      }
      return;
    }
    if (stat.size <= rt.offset) {
      if (stat.size < rt.offset) {
        rt.offset = 0;
        rt.remainder = '';
      }
      return;
    }
    const fh = await fs.open(rt.path, 'r');
    try {
      const length = stat.size - rt.offset;
      const buf = Buffer.alloc(length);
      await fh.read(buf, 0, length, rt.offset);
      rt.offset = stat.size;
      rt.remainder = parseRolloutText(rt.state, rt.remainder + buf.toString('utf8'));
    } finally {
      await fh.close();
    }
  }

  private async refreshIndex(): Promise<void> {
    if (Date.now() - this.scanAt > 5000) {
      this.scanAt = Date.now();
      await this.scanRollouts();
    }
    const file = path.join(codexHome(), 'session_index.jsonl');
    let text = '';
    try {
      text = await fs.readFile(file, 'utf8');
    } catch {
      return;
    }
    const byId = new Map<string, IndexEntry>();
    for (const line of text.split('\n')) {
      if (!line.trim()) continue;
      try {
        const obj = JSON.parse(line);
        if (typeof obj.id !== 'string') continue;
        const ts = Date.parse(obj.updated_at ?? '');
        const entry: IndexEntry = {
          id: obj.id,
          title: String(obj.thread_name ?? obj.id),
          updatedAt: Number.isFinite(ts) ? ts : 0,
        };
        const prev = byId.get(entry.id);
        if (!prev || prev.updatedAt < entry.updatedAt) byId.set(entry.id, entry);
      } catch {
        // ignore malformed line
      }
    }
    this.recent = [...byId.values()]
      .filter((e) => !this.subagentIds.has(e.id))
      .sort((a, b) => b.updatedAt - a.updatedAt);
    await this.rankByActivity();
  }

  private async rankByActivity(): Promise<void> {
    const now = new Date();
    const dirs: string[] = [];
    for (let offset = 0; offset < 2; offset++) {
      const d = new Date(now.getTime() - offset * 86400000);
      dirs.push(
        path.join(
          codexHome(),
          'sessions',
          String(d.getFullYear()),
          String(d.getMonth() + 1).padStart(2, '0'),
          String(d.getDate()).padStart(2, '0'),
        ),
      );
    }
    for (const dir of dirs) {
      let files: string[] = [];
      try {
        files = await fs.readdir(dir);
      } catch {
        continue;
      }
      for (const file of files) {
        if (!file.endsWith('.jsonl') || file.includes('_')) continue;
        const ids = file.match(UUID_RE);
        if (!ids?.length) continue;
        const id = ids[0];
        try {
          const stat = await fs.stat(path.join(dir, file));
          const prev = this.lastSizes.get(id);
          if (prev !== undefined && stat.size > prev) {
            this.activeAt.set(id, Date.now());
          }
          this.lastSizes.set(id, stat.size);
          if (stat.size > 0 && !this.recent.some((e) => e.id === id)) {
            this.recent.push({ id, title: id, updatedAt: 0 });
          }
        } catch {
          // ignore
        }
      }
    }
    let activeId: string | null = null;
    let activeTime = 0;
    for (const [id, at] of this.activeAt) {
      if (at > activeTime && Date.now() - at < 30000) {
        activeTime = at;
        activeId = id;
      }
    }
    for (const entry of this.recent) {
      const at = this.activeAt.get(entry.id);
      if (at) entry.updatedAt = Math.max(entry.updatedAt, at);
    }
    if (activeId) {
      const entry = this.recent.find((e) => e.id === activeId);
      if (entry) entry.updatedAt = Math.max(entry.updatedAt, activeTime);
    }
    this.recent.sort((a, b) => b.updatedAt - a.updatedAt);
  }

  private async scanRollouts(): Promise<void> {
    const root = path.join(codexHome(), 'sessions');
    const map = new Map<string, string>();
    const subs = new Set<string>();
    const walk = async (dir: string, depth: number): Promise<void> => {
      if (depth > 4) return;
      try {
        const entries = await fs.readdir(dir, { withFileTypes: true });
        for (const ent of entries) {
          const full = path.join(dir, ent.name);
          if (ent.isDirectory()) {
            await walk(full, depth + 1);
            continue;
          }
          if (!ent.name.endsWith('.jsonl')) continue;
          const ids = ent.name.match(UUID_RE) ?? [];
          if (!ids.length) continue;
          const isSub = ent.name.includes('_');
          for (const id of ids) map.set(id, full);
          if (isSub) {
            for (let i = 1; i < ids.length; i++) subs.add(ids[i]);
          }
        }
      } catch {
        return;
      }
    };
    await walk(root, 0);
    this.rolloutById = map;
    this.subagentIds = subs;
  }

  private async findRollout(id: string): Promise<string | null> {
    if (this.rolloutById.has(id)) return this.rolloutById.get(id)!;
    this.scanAt = 0;
    await this.scanRollouts();
    return this.rolloutById.get(id) ?? null;
  }

  private emitSnapshot(): void {
    const snap = this.getSnapshot();
    const key = JSON.stringify(snap);
    if (key === this.lastEmit) return;
    this.lastEmit = key;
    this.emit('snapshot', snap);
  }
}
