import { describe, expect, it } from 'vitest';
import { applyRolloutEvent, createRolloutState } from '../src/main/session/rollout';

const at = (iso: string) => ({ timestamp: iso });

describe('rollout parser', () => {
  it('tracks running -> waiting -> running -> done', () => {
    const s = createRolloutState();
    applyRolloutEvent(s, { ...at('2026-11-16T07:00:00Z'), type: 'event_msg', payload: { type: 'task_started' } });
    expect(s.status).toBe('running');

    applyRolloutEvent(s, {
      ...at('2026-11-16T07:00:01Z'),
      type: 'response_item',
      payload: {
        type: 'function_call',
        name: 'request_user_input',
        call_id: 'call_1',
        arguments: JSON.stringify({
          questions: [
            {
              id: 'q1',
              header: '技术栈',
              question: '选哪个？',
              options: [{ label: 'A', description: 'a' }, { label: 'B', description: 'b' }],
            },
          ],
        }),
      },
    });
    expect(s.status).toBe('waiting');
    expect(s.pendingQuestion?.options).toHaveLength(2);
    expect(s.pendingCallId).toBe('call_1');

    applyRolloutEvent(s, {
      ...at('2026-11-16T07:00:02Z'),
      type: 'response_item',
      payload: { type: 'function_call_output', call_id: 'call_1', output: 'A' },
    });
    expect(s.status).toBe('running');
    expect(s.pendingQuestion).toBeNull();

    applyRolloutEvent(s, {
      ...at('2026-11-16T07:00:03Z'),
      type: 'event_msg',
      payload: { type: 'task_complete', last_agent_message: 'done' },
    });
    expect(s.status).toBe('done');
    expect(s.lastAgentMessage).toBe('done');
  });

  it('parses update_plan and turn_aborted', () => {
    const s = createRolloutState();
    applyRolloutEvent(s, {
      type: 'response_item',
      payload: {
        type: 'function_call',
        name: 'update_plan',
        arguments: JSON.stringify({
          plan: [
            { step: 'one', status: 'completed' },
            { step: 'two', status: 'inProgress' },
          ],
        }),
      },
    });
    expect(s.planSteps).toEqual([
      { step: 'one', status: 'completed' },
      { step: 'two', status: 'inProgress' },
    ]);
    applyRolloutEvent(s, { type: 'event_msg', payload: { type: 'turn_aborted' } });
    expect(s.status).toBe('aborted');
  });

  it('uses item_completed request_user_input as fallback', () => {
    const s = createRolloutState();
    applyRolloutEvent(s, {
      type: 'event_msg',
      payload: {
        type: 'item_completed',
        item: {
          type: 'FunctionCall',
          name: 'request_user_input',
          call_id: 'call_2',
          arguments: JSON.stringify({
            questions: [{ id: 'q', header: 'H', question: 'Q?', options: [{ label: 'X', description: '' }] }],
          }),
        },
      },
    });
    expect(s.status).toBe('waiting');
    expect(s.pendingQuestion?.question).toBe('Q?');
  });
});
