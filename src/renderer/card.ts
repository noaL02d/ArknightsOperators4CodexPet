import type { SessionMessage, SessionSnapshot } from '../shared/types';

const root = document.getElementById('card') as HTMLDivElement;
let snap: SessionSnapshot | null = null;

const STATUS: Record<string, { label: string; color: string }> = {
  running: { label: '运行中', color: '#59a7ff' },
  waiting: { label: '等待选择', color: '#ffc857' },
  done: { label: '已完成', color: '#6ee7a8' },
  aborted: { label: '已中止', color: '#b0b7c3' },
  failed: { label: '失败', color: '#ff6b6b' },
  idle: { label: '待机', color: '#8b93a5' },
};

function timeText(ts: number): string {
  if (!ts) return '';
  const d = new Date(ts);
  const now = new Date();
  const sameDay =
    d.getFullYear() === now.getFullYear() &&
    d.getMonth() === now.getMonth() &&
    d.getDate() === now.getDate();
  const day = sameDay ? '' : `${d.getMonth() + 1}/${d.getDate()} `;
  return `${day}${String(d.getHours()).padStart(2, '0')}:${String(d.getMinutes()).padStart(2, '0')}`;
}

function messageRow(msg: SessionMessage): HTMLElement {
  const meta = STATUS[msg.status] ?? STATUS.idle;
  const row = document.createElement('button');
  row.className = 'message';
  row.title = msg.title;
  row.addEventListener('click', () => window.petAPI.openCodex(msg.threadId));

  const dot = document.createElement('span');
  dot.className = 'msg-dot';
  dot.style.background = meta.color;

  const main = document.createElement('span');
  main.className = 'msg-main';
  const title = document.createElement('span');
  title.className = 'msg-title';
  title.textContent = msg.title || msg.threadId;
  const sub = document.createElement('span');
  sub.className = 'msg-sub';
  const wait = msg.status === 'waiting' ? ' · 等待你的选择' : '';
  const plan = msg.planSteps.length
    ? ` · ${msg.planSteps.filter((p) => p.status === 'completed').length}/${msg.planSteps.length}`
    : '';
  sub.textContent = `${meta.label}${wait}${plan} · ${timeText(msg.updatedAt)}`;
  main.append(title, sub);

  const badge = document.createElement('span');
  badge.className = 'msg-badge';
  badge.textContent = msg.status === 'running' ? '…' : '';
  row.append(dot, main, badge);
  return row;
}

function render(): void {
  if (!snap) return;
  root.innerHTML = '';
  const head = document.createElement('div');
  head.className = 'head';
  const title = document.createElement('span');
  title.className = 'head-title';
  title.textContent = `Codex 消息 · ${snap.messages.length}`;
  head.appendChild(title);
  root.appendChild(head);

  const list = document.createElement('div');
  list.className = 'list';
  for (const msg of snap.messages) list.appendChild(messageRow(msg));
  root.appendChild(list);

  const foot = document.createElement('div');
  foot.className = 'foot';
  const mark = document.createElement('button');
  mark.className = 'foot-btn';
  mark.textContent = '全部已读';
  mark.addEventListener('click', () => window.petAPI.markAllRead());
  const open = document.createElement('button');
  open.className = 'foot-btn primary';
  open.textContent = '打开 Codex';
  const first = snap.messages[0];
  open.addEventListener('click', () => window.petAPI.openCodex(first?.threadId ?? null));
  foot.append(mark, open);
  root.appendChild(foot);
}

window.petAPI.onSnapshot((next) => {
  snap = next;
  render();
});
