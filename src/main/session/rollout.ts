import type { PendingQuestion, PlanStepView, SessionStatus } from '../../shared/types';

export interface RolloutState {
  status: SessionStatus;
  planSteps: PlanStepView[];
  pendingQuestion: PendingQuestion | null;
  pendingCallId: string | null;
  lastAgentMessage: string | null;
  updatedAt: number;
}

export function createRolloutState(): RolloutState {
  return {
    status: 'idle',
    planSteps: [],
    pendingQuestion: null,
    pendingCallId: null,
    lastAgentMessage: null,
    updatedAt: 0,
  };
}

function safeJson(value: unknown): any {
  if (typeof value !== 'string') return value;
  try {
    return JSON.parse(value);
  } catch {
    return null;
  }
}

function parseQuestion(args: any): PendingQuestion | null {
  const questions = args?.questions;
  if (!Array.isArray(questions) || questions.length === 0) return null;
  for (const q of questions) {
    if (!q || typeof q.question !== 'string') continue;
    return {
      id: String(q.id ?? ''),
      header: String(q.header ?? ''),
      question: q.question,
      options: Array.isArray(q.options)
        ? q.options
            .filter((o: any) => o && typeof o.label === 'string')
            .map((o: any) => ({ label: o.label, description: String(o.description ?? '') }))
        : [],
    };
  }
  return null;
}

function parsePlan(args: any): PlanStepView[] | null {
  const plan = args?.plan;
  if (!Array.isArray(plan)) return null;
  return plan
    .filter((p: any) => p && typeof p.step === 'string')
    .map((p: any) => ({
      step: p.step,
      status:
        p.status === 'completed' || p.status === 'inProgress' || p.status === 'pending'
          ? p.status
          : 'pending',
    }));
}

const CALL_TYPES = new Set(['function_call', 'custom_tool_call']);
const OUTPUT_TYPES = new Set(['function_call_output', 'custom_tool_call_output']);

export function applyRolloutEvent(state: RolloutState, line: any): void {
  if (!line || typeof line !== 'object') return;
  const ts = Date.parse(line.timestamp ?? '');
  if (Number.isFinite(ts)) state.updatedAt = Math.max(state.updatedAt, ts);
  const payload = line.payload ?? {};

  if (line.type === 'event_msg') {
    switch (payload.type) {
      case 'task_started':
        state.status = 'running';
        state.pendingQuestion = null;
        state.pendingCallId = null;
        break;
      case 'task_complete':
        state.status = 'done';
        if (typeof payload.last_agent_message === 'string') {
          state.lastAgentMessage = payload.last_agent_message;
        }
        break;
      case 'turn_aborted':
        state.status = 'aborted';
        break;
      case 'error':
        state.status = 'failed';
        break;
      case 'item_completed': {
        const item = payload.item;
        const itemType = String(item?.type ?? '').toLowerCase();
        if (
          item &&
          (CALL_TYPES.has(itemType) || itemType === 'functioncall' || itemType === 'customtoolcall') &&
          item.name === 'request_user_input'
        ) {
          const question = parseQuestion(safeJson(item.arguments));
          if (question) {
            state.pendingQuestion = question;
            state.pendingCallId = item.call_id ?? item.id ?? null;
            state.status = 'waiting';
          }
        }
        break;
      }
      default:
        break;
    }
    return;
  }

  if (line.type === 'response_item') {
    const type = payload.type;
    if (CALL_TYPES.has(type)) {
      const args = safeJson(payload.arguments);
      if (payload.name === 'request_user_input') {
        const question = parseQuestion(args);
        if (question) {
          state.pendingQuestion = question;
          state.pendingCallId = payload.call_id ?? null;
          state.status = 'waiting';
        }
      } else if (payload.name === 'update_plan') {
        const plan = parsePlan(args);
        if (plan) state.planSteps = plan;
      }
      return;
    }
    if (OUTPUT_TYPES.has(type)) {
      if (state.pendingCallId && payload.call_id === state.pendingCallId) {
        state.pendingQuestion = null;
        state.pendingCallId = null;
        if (state.status === 'waiting') state.status = 'running';
      }
      return;
    }
    if (type === 'message' && payload.role === 'assistant') {
      const content = Array.isArray(payload.content) ? payload.content : [];
      const text = content
        .filter((c: any) => c && (c.type === 'output_text' || c.type === 'text'))
        .map((c: any) => c.text)
        .filter((t: unknown) => typeof t === 'string')
        .join('\n')
        .trim();
      if (text) state.lastAgentMessage = text;
    }
  }
}

export function parseRolloutText(state: RolloutState, text: string): string {
  const lines = text.split('\n');
  const remainder = lines.pop() ?? '';
  for (const line of lines) {
    const trimmed = line.trim();
    if (!trimmed) continue;
    try {
      applyRolloutEvent(state, JSON.parse(trimmed));
    } catch {
      // Ignore partial or unknown lines.
    }
  }
  return remainder;
}
