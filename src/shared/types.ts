export type Form = 'base' | 'battle';
export type BaseState = 'stand' | 'sit' | 'lie';
export type BattleState = 'default' | 'skill1' | 'skill2' | 'skill3';
export type DragMode = 'none' | 'move' | 'skill3move';

export interface Step {
  model: Form;
  anim: string;
  loop: boolean;
}

export interface PetPlan {
  steps: Step[];
  dragMode: DragMode;
  busy: boolean;
}

export interface PetConfig {
  operatorId: string;
  form: Form;
  baseState: BaseState;
  battleState: BattleState;
  scale: number;
  position: { x: number; y: number } | null;
  pinnedThreadId: string | null;
  sessionCardVisible: boolean;
  alwaysOnTop: boolean;
  autostart: boolean;
  closeWithCodex: boolean;
  viewedThreads: Record<string, number>;
}

export interface ModelFiles {
  skel: string;
  atlas: string;
  texture: string;
}

export interface CodexPetStateMapping {
  model: Form;
  anim: string;
  frames: number;
  flip?: boolean;
}

export interface OperatorManifest {
  id: string;
  name: string;
  avatar: string;
  spineVersion: string;
  models: Record<Form, ModelFiles>;
  animations: Record<Form, Record<string, string>>;
  rootBone: string;
  rootClamp: number;
  referenceHeight: number;
  referenceWidth: number;
  codexPet: {
    version: 2;
    states: Record<string, CodexPetStateMapping>;
  };
}

export type SessionStatus = 'idle' | 'running' | 'waiting' | 'done' | 'aborted' | 'failed';

export interface PlanStepView {
  step: string;
  status: 'pending' | 'inProgress' | 'completed';
}

export interface PendingQuestion {
  id: string;
  header: string;
  question: string;
  options: Array<{ label: string; description: string }>;
}

export interface SessionSnapshot {
  threadId: string | null;
  title: string;
  status: SessionStatus;
  planSteps: PlanStepView[];
  pendingQuestion: PendingQuestion | null;
  lastAgentMessage: string | null;
  updatedAt: number;
  messages: SessionMessage[];
  dot: 'gray' | 'blue' | 'green';
}

export interface SessionMessage {
  threadId: string;
  title: string;
  status: SessionStatus;
  planSteps: PlanStepView[];
  pendingQuestion: PendingQuestion | null;
  lastAgentMessage: string | null;
  updatedAt: number;
  unread: boolean;
}

export interface RecentThread {
  id: string;
  title: string;
  updatedAt: number;
}

export interface MenuState {
  operatorId: string;
  operators: Array<{ id: string; name: string }>;
  form: Form;
  baseState: BaseState;
  battleState: BattleState;
  scale: number;
  pinnedThreadId: string | null;
  recentThreads: RecentThread[];
  sessionCardVisible: boolean;
  alwaysOnTop: boolean;
  autostart: boolean;
  closeWithCodex: boolean;
}

export interface InitPayload {
  config: PetConfig;
  operator: OperatorManifest;
  operators: Array<{ id: string; name: string }>;
}

export const BATTLE_STATE_LABELS: Record<BattleState, string> = {
  default: '默认',
  skill1: '技能一',
  skill2: '技能二',
  skill3: '技能三',
};

export const BASE_STATE_LABELS: Record<BaseState, string> = {
  stand: '站',
  sit: '坐',
  lie: '躺',
};
