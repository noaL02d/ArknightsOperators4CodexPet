import { PetStateMachine } from '../shared/petState';
import type { BaseState, BattleState, Form, OperatorManifest, PetPlan, SessionSnapshot } from '../shared/types';
import { PetSpine } from './spine';

const canvas = document.getElementById('pet') as HTMLCanvasElement;
const statusDot = document.getElementById('status-dot') as HTMLDivElement;

let manifest: OperatorManifest;
let player: PetSpine;
let sm: PetStateMachine;
let scale = 1;
let currentThreadId: string | null = null;

let down = false;
let dragging = false;
let downX = 0;
let downY = 0;
let lastClickAt = 0;
let lastWheelAt = 0;
let lastDragScreenX = 0;

function applyPlan(plan: PetPlan): void {
  player.play(plan);
  updateDotPosition();
  void window.petAPI.saveConfig({
    form: sm.form,
    baseState: sm.baseState,
    battleState: sm.battleState,
  });
}

function updateDotPosition(): void {
  if (!player || !sm) return;
  const key =
    sm.form === 'base'
      ? sm.baseState === 'sit'
        ? 'sitIdle'
        : sm.baseState === 'lie'
          ? 'lieIdle'
          : 'standIdle'
      : sm.battleState === 'skill1'
        ? 'skill1Idle'
        : sm.battleState === 'skill2'
          ? 'skill2Idle'
          : sm.battleState === 'skill3'
            ? 'skill3Idle'
            : 'idle';
  const p = player.getDotPosition(sm.form, key);
  statusDot.style.left = `${p.x - 5}px`;
  statusDot.style.top = `${p.y - 5}px`;
  statusDot.style.right = 'auto';
  window.petAPI.setEarTip(p.y);
}

function setStatus(snap: SessionSnapshot): void {
  currentThreadId = snap.threadId;
  statusDot.className = `dot ${snap.dot}`;
  statusDot.title = `${snap.title} · ${snap.status}`;
  statusDot.style.display = 'block';
}

async function init(): Promise<void> {
  const initPayload = await window.petAPI.init();
  manifest = initPayload.operator;
  scale = initPayload.config.scale;
  sm = new PetStateMachine({
    form: initPayload.config.form,
    baseState: initPayload.config.baseState,
    battleState: initPayload.config.battleState,
  });
  player = await PetSpine.create(canvas, manifest, sm.form, () => {
    sm.markIdle();
  });
  player.resize();
  applyPlan(sm.idlePlan());
  window.addEventListener('resize', () => {
    player.resize();
    updateDotPosition();
  });

  requestAnimationFrame(frame);
}

let lastFrame = performance.now();
function frame(now: number): void {
  const dt = Math.min(0.1, (now - lastFrame) / 1000);
  lastFrame = now;
  if (player) {
    player.update(dt);
    player.render();
  }
  requestAnimationFrame(frame);
}

function endPointer(): void {
  if (!down) return;
  down = false;
  if (dragging) {
    dragging = false;
    const plan = sm.dragEnd();
    applyPlan(plan);
    window.petAPI.dragEnd();
    canvas.style.cursor = 'grab';
  }
}

function abortDrag(): void {
  if (!dragging) return;
  dragging = false;
  down = false;
  window.petAPI.dragEnd();
  applyPlan(sm.idlePlan());
}

function onPointerDown(event: PointerEvent): void {
  if (event.button === 1) {
    if (!player?.hitTest(event.clientX, event.clientY)) return;
    event.preventDefault();
    if (dragging) abortDrag();
    scale = 0.56;
    void window.petAPI.resetScale();
    return;
  }
  if (event.button !== 0) return;
  event.preventDefault();
  down = true;
  dragging = false;
  downX = event.clientX;
  downY = event.clientY;
  lastDragScreenX = event.screenX;
  canvas.setPointerCapture(event.pointerId);
}

function onPointerMove(event: PointerEvent): void {
  if (down && !dragging) {
    const dist = Math.hypot(event.clientX - downX, event.clientY - downY);
    if (dist > 4) {
      dragging = true;
      const plan = sm.dragStart();
      applyPlan(plan);
      const shouldMove = plan.dragMode === 'move' || plan.dragMode === 'skill3move';
      window.petAPI.dragStart(shouldMove);
      canvas.style.cursor = shouldMove ? 'grabbing' : 'default';
    }
    return;
  }
  if (dragging) {
    const dx = event.screenX - lastDragScreenX;
    if (Math.abs(dx) >= 2) {
      player.setFacing(dx > 0 ? 1 : -1);
      lastDragScreenX = event.screenX;
    }
    return;
  }
  const hit = player?.hitTest(event.clientX, event.clientY) ?? false;
  canvas.style.cursor = hit ? 'grab' : 'default';
}

function onPointerUp(event: PointerEvent): void {
  if (event.button !== 0) return;
  if (!down) return;
  const wasDragging = dragging;
  endPointer();
  if (wasDragging) return;
  const now = performance.now();
  if (now - lastClickAt < 300) {
    lastClickAt = 0;
    window.petAPI.openCodex(currentThreadId);
    return;
  }
  lastClickAt = now;
  applyPlan(sm.click());
}

function onWheel(event: WheelEvent): void {
  if (!dragging) return;
  if (Math.abs(event.deltaX) > Math.abs(event.deltaY)) return;
  if (Math.abs(event.deltaY) < 1) return;
  event.preventDefault();
  const now = performance.now();
  if (now - lastWheelAt < 100) return;
  lastWheelAt = now;
  const step = event.deltaY < 0 ? 0.06 : -0.06;
  const next = Math.min(2, Math.max(0.4, scale + step));
  if (Math.abs(next - scale) < 0.001) return;
  scale = next;
  void window.petAPI.setScale(scale).then((value) => {
    scale = value;
  });
}

function onPointerLeave(): void {
  // Intentionally disabled for now: per-pixel click-through can race with
  // the first click after the cursor enters the window.
}

window.addEventListener('pointerdown', onPointerDown);
window.addEventListener('pointermove', onPointerMove);
window.addEventListener('pointerup', onPointerUp);
window.addEventListener('pointercancel', () => endPointer());
window.addEventListener('lostpointercapture', () => endPointer());
window.addEventListener('blur', () => abortDrag());
window.addEventListener('wheel', onWheel, { passive: false });
window.addEventListener('pointerleave', onPointerLeave);
window.addEventListener('contextmenu', (event) => {
  event.preventDefault();
  window.petAPI.openMenu();
});

window.petAPI.onSide((button) => {
  if (dragging || sm.busy) return;
  const plan = button === 'forward' ? sm.toggleForm() : sm.cycleState();
  applyPlan(plan);
});

window.petAPI.onApply((action) => {
  if (dragging) return;
  if (action.type === 'reload') {
    window.location.reload();
    return;
  }
  if (sm.busy) return;
  if (action.type === 'form') applyPlan(sm.setForm(action.value as Form));
  if (action.type === 'baseState') applyPlan(sm.setBaseState(action.value as BaseState));
  if (action.type === 'battleState') applyPlan(sm.setBattleState(action.value as BattleState));
});

window.petAPI.onSnapshot(setStatus);
window.petAPI.onScale((value) => {
  scale = value;
});

init().catch((err) => {
  console.error(err);
  document.body.innerHTML = `<pre style="color:#f66;font:12px monospace;padding:8px;white-space:pre-wrap">${String(
    err instanceof Error ? err.stack ?? err.message : err,
  )}</pre>`;
});
