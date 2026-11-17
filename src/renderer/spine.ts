import type { Form, OperatorManifest, PetPlan, Step } from '../shared/types';

declare const spine: any;

export interface LoadedModel {
  data: any;
  atlas: any;
  textures: any[];
  idleAnchors: Record<string, { x: number; y: number }>;
  idleBoxes: Record<string, { x: number; y: number; width: number; height: number }>;
}

interface ScreenRect {
  left: number;
  top: number;
  right: number;
  bottom: number;
}

const IDLE_KEYS: Record<Form, string[]> = {
  base: ['standIdle', 'sitIdle', 'lieIdle'],
  battle: ['idle', 'skill1Idle', 'skill2Idle', 'skill3Idle'],
};

function pageNames(atlasText: string): string[] {
  const pages: string[] = [];
  const lines = atlasText.split(/\r?\n/);
  for (let i = 0; i < lines.length; i++) {
    const line = lines[i];
    if (!line.trim()) continue;
    if (/^\s/.test(line)) continue;
    if (line.includes(':')) continue;
    const next = lines.slice(i + 1).find((l) => l.trim());
    if (next && /^\s*size\s*:/i.test(next)) pages.push(line.trim());
  }
  return pages;
}

function loadImage(url: string): Promise<HTMLImageElement> {
  return new Promise((resolve, reject) => {
    const img = new Image();
    img.onload = () => resolve(img);
    img.onerror = () => reject(new Error(`image load failed: ${url}`));
    img.src = url;
  });
}

async function loadText(url: string): Promise<string> {
  const res = await fetch(url);
  if (!res.ok) throw new Error(`${res.status} ${url}`);
  return res.text();
}

async function loadBinary(url: string): Promise<Uint8Array> {
  const res = await fetch(url);
  if (!res.ok) throw new Error(`${res.status} ${url}`);
  return new Uint8Array(await res.arrayBuffer());
}

export async function loadSpineModel(
  context: any,
  manifest: OperatorManifest,
  form: Form,
): Promise<LoadedModel> {
  const files = manifest.models[form];
  const [atlasText, skelBytes] = await Promise.all([loadText(files.atlas), loadBinary(files.skel)]);
  const pages = pageNames(atlasText);
  const images = await Promise.all(
    pages.map((p) => loadImage(new URL(p, files.atlas).toString())),
  );
  let imageIndex = 0;
  const atlas = new spine.TextureAtlas(atlasText, () => {
    const img = images[imageIndex++] ?? images[0];
    return new spine.webgl.GLTexture(context, img, false);
  });
  const data = new spine.SkeletonBinary(new spine.AtlasAttachmentLoader(atlas)).readSkeletonData(
    skelBytes,
  );
  const idleAnchors: Record<string, { x: number; y: number }> = {};
  const idleBoxes: Record<
    string,
    { x: number; y: number; width: number; height: number }
  > = {};
  for (const key of IDLE_KEYS[form]) {
    const animName = manifest.animations[form][key];
    if (!animName) continue;
    const pose = computeAnchor(data, animName);
    idleAnchors[key] = pose.anchor;
    idleBoxes[key] = pose.box;
  }
  return { data, atlas, textures: [], idleAnchors, idleBoxes };
}

function computeAnchor(data: any, animName: string): {
  anchor: { x: number; y: number };
  box: { x: number; y: number; width: number; height: number };
} {
  const skeleton = new spine.Skeleton(data);
  const state = new spine.AnimationState(new spine.AnimationStateData(data));
  state.setAnimation(0, animName, false);
  state.update(0.3);
  state.apply(skeleton);
  skeleton.updateWorldTransform();
  const offset = new spine.Vector2();
  const size = new spine.Vector2();
  const temp = new spine.Vector2();
  skeleton.getBounds(offset, size, temp);
  return {
    anchor: { x: offset.x + size.x / 2, y: offset.y },
    box: { x: offset.x, y: offset.y, width: size.x, height: size.y },
  };
}

export class PetSpine {
  private manifest: OperatorManifest;
  private canvas: HTMLCanvasElement;
  private gl: WebGLRenderingContext;
  private renderer: any;
  private models: Record<Form, LoadedModel>;
  private form: Form;
  private skeleton: any = null;
  private state: any = null;
  private activeEntry: any = null;
  private plan: PetPlan | null = null;
  private stepIndex = 0;
  private cameraAnchor = { x: 0, y: 0 };
  private targetAnchor = { x: 0, y: 0 };
  private anchorBase = { x: 0, y: 0 };
  private facing: 1 | -1 = 1;
  private bounds = { x: 0, y: 0, width: 1, height: 1 };
  private baseZoom = 1;
  private displayZoom = 1;
  private ready = false;
  private onIdle: () => void;

  private constructor(
    canvas: HTMLCanvasElement,
    gl: WebGLRenderingContext,
    manifest: OperatorManifest,
    models: Record<Form, LoadedModel>,
    initialForm: Form,
    onIdle: () => void,
  ) {
    this.canvas = canvas;
    this.gl = gl;
    this.manifest = manifest;
    this.models = models;
    this.form = initialForm;
    this.onIdle = onIdle;
    this.renderer = new spine.webgl.SceneRenderer(canvas, gl, false);
    this.renderer.skeletonRenderer.premultipliedAlpha = true;
    this.setModel(initialForm);
    this.resize();
    this.ready = true;
  }

  static async create(
    canvas: HTMLCanvasElement,
    manifest: OperatorManifest,
    initialForm: Form,
    onIdle: () => void,
  ): Promise<PetSpine> {
    const gl = canvas.getContext('webgl', {
      alpha: true,
      premultipliedAlpha: true,
      preserveDrawingBuffer: true,
      antialias: true,
    });
    if (!gl) throw new Error('WebGL unavailable');
    const renderer = new spine.webgl.SceneRenderer(canvas, gl, false);
    const models = {
      base: await loadSpineModel(renderer.context, manifest, 'base'),
      battle: await loadSpineModel(renderer.context, manifest, 'battle'),
    };
    return new PetSpine(canvas, gl, manifest, models, initialForm, onIdle);
  }

  private setModel(form: Form): void {
    this.form = form;
    const model = this.models[form];
    this.skeleton = new spine.Skeleton(model.data);
    this.skeleton.setToSetupPose();
    const stateData = new spine.AnimationStateData(model.data);
    stateData.defaultMix = 0.12;
    this.state = new spine.AnimationState(stateData);
    this.state.addListener({
      complete: (entry: any) => {
        if (entry === this.activeEntry) this.advance();
      },
    });
    this.activeEntry = null;
  }

  private anchorFor(step: Step): { x: number; y: number } {
    const anchors = this.models[step.model].idleAnchors;
    if (anchors[step.anim]) return anchors[step.anim];
    const fallback: Record<string, string> = {
      move: 'standIdle',
      interact: 'standIdle',
      attack: 'idle',
      start: 'idle',
      start2: 'skill1Idle',
      start2Begin: 'skill2Idle',
      start3Begin: 'skill3Idle',
      restart3: 'skill3Idle',
      die: 'idle',
      skill1Die: 'skill1Idle',
      skill1End: 'skill1Idle',
      skill1Loop: 'skill1Idle',
      skill2Die: 'skill2Idle',
      skill2End: 'skill2Idle',
      skill2Loop: 'skill2Idle',
      skill2Begin: 'skill2Idle',
      skill2TakeoffBegin: 'skill2Idle',
      skill2TakeoffEnd: 'skill2Idle',
      skill2TakeoffLoop: 'skill2Idle',
      skill3Die: 'skill3Idle',
      skill3End: 'skill3Idle',
      skill3Loop: 'skill3Idle',
      skill3Combat: 'skill3Idle',
      skill3Move: 'skill3Idle',
    };
    const alt = fallback[step.anim];
    return (alt && anchors[alt]) || { x: 0, y: 0 };
  }

  play(plan: PetPlan): void {
    if (!this.ready) return;
    const steps = plan.steps.filter((s) => Boolean(this.manifest.animations[s.model][s.anim]));
    if (!steps.length) return;
    this.plan = { ...plan, steps };
    this.stepIndex = 0;
    this.anchorBase = this.anchorFor(steps[steps.length - 1]);
    this.targetAnchor = { x: this.anchorBase.x * this.facing, y: this.anchorBase.y };
    this.playStep(0);
  }

  private playStep(index: number): void {
    if (!this.plan) return;
    const step = this.plan.steps[index];
    if (!step) {
      this.onIdle();
      return;
    }
    if (step.model !== this.form) this.setModel(step.model);
    const animName = this.manifest.animations[step.model][step.anim];
    const entry = this.state.setAnimation(0, animName, step.loop);
    const isLast = index === this.plan.steps.length - 1;
    if (step.loop) {
      this.activeEntry = null;
      if (isLast) this.onIdle();
    } else {
      this.activeEntry = entry;
    }
  }

  private advance(): void {
    if (!this.plan) return;
    const next = this.stepIndex + 1;
    if (next >= this.plan.steps.length) {
      this.activeEntry = null;
      this.onIdle();
      return;
    }
    this.stepIndex = next;
    this.playStep(next);
  }

  resize(): void {
    const dpr = Math.min(window.devicePixelRatio || 1, 2.5);
    const cssW = Math.max(1, this.canvas.clientWidth);
    const cssH = Math.max(1, this.canvas.clientHeight);
    this.canvas.width = Math.round(cssW * dpr);
    this.canvas.height = Math.round(cssH * dpr);
    this.baseZoom = ((this.manifest.referenceHeight ?? 520) * 1.08) / this.canvas.height;
    this.displayZoom = this.baseZoom;
    this.renderer.camera = new spine.webgl.OrthoCamera(this.canvas.width, this.canvas.height);
    this.gl.viewport(0, 0, this.canvas.width, this.canvas.height);
  }

  update(dt: number): void {
    if (!this.ready || !this.skeleton || !this.state) return;
    this.state.update(Math.min(dt, 0.05));
    this.state.apply(this.skeleton);
    const bone = this.skeleton.findBone(this.manifest.rootBone);
    if (bone) {
      const clamp = this.manifest.rootClamp ?? 80;
      const bx = bone.data.x;
      const by = bone.data.y;
      bone.x = Math.max(bx - clamp, Math.min(bx + clamp, bone.x));
      bone.y = Math.max(by - clamp, Math.min(by + clamp, bone.y));
    }
    this.skeleton.scaleX = this.facing;
    this.skeleton.updateWorldTransform();
    const offset = new spine.Vector2();
    const size = new spine.Vector2();
    const temp = new spine.Vector2();
    this.skeleton.getBounds(offset, size, temp);
    this.bounds = { x: offset.x, y: offset.y, width: Math.max(1, size.x), height: Math.max(1, size.y) };
    // Keep the camera zoom fixed. Animation bounds change constantly; fitting
    // them dynamically makes the character visibly breathe in and out.
    this.displayZoom = this.baseZoom;
    const lerp = Math.min(1, dt * 9);
    this.cameraAnchor.x += (this.targetAnchor.x - this.cameraAnchor.x) * lerp;
    this.cameraAnchor.y += (this.targetAnchor.y - this.cameraAnchor.y) * lerp;
  }

  private updateCamera(): void {
    const camera = this.renderer.camera;
    camera.zoom = this.displayZoom;
    const bottomMargin = 8 * (this.canvas.height / Math.max(1, this.canvas.clientHeight));
    camera.position.x = this.cameraAnchor.x;
    camera.position.y =
      this.cameraAnchor.y + this.displayZoom * this.canvas.height / 2 - this.displayZoom * bottomMargin;
  }

  render(): void {
    if (!this.ready || !this.skeleton) return;
    this.updateCamera();
    this.gl.clearColor(0, 0, 0, 0);
    this.gl.clear(this.gl.COLOR_BUFFER_BIT);
    this.renderer.begin();
    this.renderer.drawSkeleton(this.skeleton, true);
    this.renderer.end();
  }

  hitTest(clientX: number, clientY: number): boolean {
    if (!this.ready) return false;
    const cssW = this.canvas.clientWidth;
    const cssH = this.canvas.clientHeight;
    const sx = (x: number) => (x - this.renderer.camera.position.x) / this.displayZoom + cssW / 2;
    const sy = (y: number) =>
      cssH / 2 - (y - this.renderer.camera.position.y) / this.displayZoom;
    const left = sx(this.bounds.x);
    const right = sx(this.bounds.x + this.bounds.width);
    const top = sy(this.bounds.y + this.bounds.height);
    const bottom = sy(this.bounds.y);
    return (
      clientX >= left - 6 &&
      clientX <= right + 6 &&
      clientY >= top - 6 &&
      clientY <= bottom + 6
    );
  }

  setFacing(facing: 1 | -1): void {
    if (facing === this.facing) return;
    this.facing = facing;
    this.targetAnchor.x = this.anchorBase.x * facing;
  }

  getDotPosition(form: Form, key: string): { x: number; y: number } {
    const box = this.models[form].idleBoxes[key];
    const anchor = this.models[form].idleAnchors[key];
    const cssW = this.canvas.clientWidth;
    const cssH = this.canvas.clientHeight;
    if (!box || !anchor) return { x: cssW - 16, y: 16 };
    const zoom = this.baseZoom;
    const bottomMargin = 8 * (this.canvas.height / Math.max(1, cssH));
    const cameraX = anchor.x;
    const cameraY = anchor.y + (zoom * this.canvas.height) / 2 - zoom * bottomMargin;
    const worldX = box.x + box.width - 4;
    const worldY = box.y + box.height - 4;
    const x = cssW / 2 + (worldX - cameraX) / zoom;
    const y = cssH / 2 - (worldY - cameraY) / zoom;
    return {
      x: Math.min(Math.max(8, x), cssW - 8),
      y: Math.min(Math.max(8, y), cssH - 8),
    };
  }

  getTopScreenY(): number {
    if (!this.ready) return 0;
    const cssH = this.canvas.clientHeight;
    let worldTop = this.bounds.y + this.bounds.height;
    const earL =
      this.skeleton.findBone('F_L_Ear_B2') ??
      this.skeleton.findBone('F_L_Ear_B') ??
      this.skeleton.findBone('F_L_Ear_A');
    const earR =
      this.skeleton.findBone('F_R_Ear_B2') ??
      this.skeleton.findBone('F_R_Ear_B') ??
      this.skeleton.findBone('F_R_Ear_A');
    if (earL || earR) {
      worldTop = Math.max(earL?.worldY ?? -Infinity, earR?.worldY ?? -Infinity) + 8;
    } else {
      worldTop = this.bounds.y + this.bounds.height * 0.94;
    }
    return cssH / 2 - (worldTop - this.renderer.camera.position.y) / this.displayZoom;
  }

  getForm(): Form {
    return this.form;
  }
}
