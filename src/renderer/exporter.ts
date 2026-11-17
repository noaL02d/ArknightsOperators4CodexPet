import type { Form, OperatorManifest } from '../shared/types';
import { loadSpineModel, type LoadedModel } from './spine';

declare const spine: any;

const SHEET_W = 1536;
const SHEET_H = 2288;
const CELL_W = 192;
const CELL_H = 208;
const COLS = 8;
const ROWS = 11;
const REQUIRED = [6, 8, 8, 4, 5, 8, 6, 6, 6, 8, 8];
const ROW_STATES = [
  'idle',
  'running-right',
  'running-left',
  'waving',
  'jumping',
  'failed',
  'waiting',
  'running',
  'review',
];

interface Frame {
  row: number;
  col: number;
  model: Form;
  anim: string;
  t: number;
  flip: boolean;
}

function pose(data: any, animName: string, t: number) {
  const skeleton = new spine.Skeleton(data);
  const state = new spine.AnimationState(new spine.AnimationStateData(data));
  state.setAnimation(0, animName, false);
  state.update(t);
  state.apply(skeleton);
  skeleton.updateWorldTransform();
  const offset = new spine.Vector2();
  const size = new spine.Vector2();
  const temp = new spine.Vector2();
  skeleton.getBounds(offset, size, temp);
  return {
    skeleton,
    bounds: { x: offset.x, y: offset.y, width: Math.max(1, size.x), height: Math.max(1, size.y) },
  };
}

async function run(operator: OperatorManifest): Promise<void> {
  const renderCanvas = document.createElement('canvas');
  renderCanvas.width = 384;
  renderCanvas.height = 416;
  const gl = renderCanvas.getContext('webgl', {
    alpha: true,
    premultipliedAlpha: true,
    preserveDrawingBuffer: true,
    antialias: true,
  });
  if (!gl) throw new Error('WebGL unavailable');
  const renderer = new spine.webgl.SceneRenderer(renderCanvas, gl, false);
  const models: Record<Form, LoadedModel> = {
    base: await loadSpineModel(renderer.context, operator, 'base'),
    battle: await loadSpineModel(renderer.context, operator, 'battle'),
  };

  const sheet = document.createElement('canvas');
  sheet.width = SHEET_W;
  sheet.height = SHEET_H;
  const ctx = sheet.getContext('2d');
  if (!ctx) throw new Error('2d context unavailable');

  const frames: Frame[] = [];
  const addState = (row: number, stateKey: string, fallbackKey?: string) => {
    const mapping =
      operator.codexPet.states[stateKey] ??
      (fallbackKey ? operator.codexPet.states[fallbackKey] : undefined);
    if (!mapping) return;
    const count = Math.min(COLS, mapping.frames);
    const data = models[mapping.model].data;
    const animName = operator.animations[mapping.model][mapping.anim];
    if (!animName || !data.findAnimation(animName)) return;
    for (let i = 0; i < count; i++) {
      frames.push({
        row,
        col: i,
        model: mapping.model,
        anim: animName,
        t: i,
        flip: Boolean(mapping.flip),
      });
    }
  };

  ROW_STATES.forEach((key, row) => addState(row, key, key === 'running' ? 'running-right' : undefined));
  // 16 look directions: mirrored / normal standing frames.
  for (let i = 0; i < 8; i++) {
    frames.push({ row: 9, col: i, model: 'base', anim: operator.animations.base.standIdle, t: i, flip: true });
    frames.push({ row: 10, col: i, model: 'base', anim: operator.animations.base.standIdle, t: i, flip: false });
  }

  // First pass: find a common scale that fits every frame.
  let maxW = 1;
  let maxH = 1;
  const withTime = (frame: Frame) => {
    const data = models[frame.model].data;
    const anim = data.findAnimation(frame.anim);
    const duration = anim?.duration ?? 0;
    const total = REQUIRED[frame.row];
    return duration > 0 ? (duration * frame.t) / total : 0;
  };
  for (const frame of frames) {
    const { bounds } = pose(models[frame.model].data, frame.anim, withTime(frame));
    maxW = Math.max(maxW, bounds.width);
    maxH = Math.max(maxH, bounds.height);
  }
  const zoom = Math.max(maxW / (renderCanvas.width * 0.94), maxH / (renderCanvas.height * 0.94));

  for (const frame of frames) {
    const { skeleton, bounds } = pose(models[frame.model].data, frame.anim, withTime(frame));
    const camera = new spine.webgl.OrthoCamera(renderCanvas.width, renderCanvas.height);
    camera.zoom = zoom;
    camera.position.x = bounds.x + bounds.width / 2;
    camera.position.y = bounds.y + (zoom * renderCanvas.height) / 2 - zoom * 10;
    renderer.camera = camera;
    gl.clearColor(0, 0, 0, 0);
    gl.clear(gl.COLOR_BUFFER_BIT);
    renderer.begin();
    renderer.drawSkeleton(skeleton, true);
    renderer.end();
    const x = frame.col * CELL_W;
    const y = frame.row * CELL_H;
    ctx.save();
    if (frame.flip) {
      ctx.translate(x + CELL_W, y);
      ctx.scale(-1, 1);
      ctx.drawImage(renderCanvas, 0, 0, CELL_W, CELL_H);
    } else {
      ctx.drawImage(renderCanvas, x, y, CELL_W, CELL_H);
    }
    ctx.restore();
  }

  // Validate required cells.
  for (let row = 0; row < ROWS; row++) {
    for (let col = 0; col < REQUIRED[row]; col++) {
      const img = ctx.getImageData(col * CELL_W, row * CELL_H, CELL_W, CELL_H).data;
      let painted = 0;
      let transparent = 0;
      for (let i = 3; i < img.length; i += 4) {
        if (img[i] > 8) painted++;
        if (img[i] < 248) transparent++;
      }
      if (painted === 0) console.warn(`empty cell row=${row} col=${col}`);
      if (transparent === 0) console.warn(`opaque cell row=${row} col=${col}`);
    }
  }

  const dataUrl = sheet.toDataURL('image/webp', 0.92);
  window.petAPI.sendNativePetData(dataUrl);
  document.body.textContent = 'done';
}

window.petAPI.exporterInit((payload) => {
  run(payload.operator).catch((err) => {
    console.error(err);
    document.body.textContent = String(err instanceof Error ? err.stack ?? err.message : err);
  });
});
