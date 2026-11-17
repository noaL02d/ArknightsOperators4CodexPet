import { describe, expect, it } from 'vitest';
import { PetStateMachine } from '../src/shared/petState';

const seq = (sm: PetStateMachine, plan: ReturnType<PetStateMachine['click']>) =>
  plan.steps.map((s) => `${s.model}:${s.anim}${s.loop ? ':loop' : ''}`);

describe('PetStateMachine', () => {
  it('uses Relax/Sit/Sleep as base idle loops', () => {
    const sm = new PetStateMachine();
    expect(seq(sm, sm.idlePlan())).toEqual(['base:standIdle:loop']);
    sm.setBaseState('sit');
    expect(seq(sm, sm.idlePlan())).toEqual(['base:sitIdle:loop']);
    sm.setBaseState('lie');
    expect(seq(sm, sm.idlePlan())).toEqual(['base:lieIdle:loop']);
  });

  it('cycles base states stand -> sit -> lie -> stand', () => {
    const sm = new PetStateMachine();
    expect(sm.cycleState().steps.at(-1)?.anim).toBe('sitIdle');
    expect(sm.cycleState().steps.at(-1)?.anim).toBe('lieIdle');
    expect(sm.cycleState().steps.at(-1)?.anim).toBe('standIdle');
  });

  it('enters battle with the right start animation per state', () => {
    const cases = [
      ['default', ['battle:start', 'battle:idle:loop']],
      ['skill1', ['battle:start2', 'battle:skill1Idle:loop']],
      ['skill2', ['battle:start2Begin', 'battle:skill2Idle:loop']],
      ['skill3', ['battle:start', 'battle:restart3', 'battle:skill3Idle:loop']],
    ] as const;
    for (const [state, expected] of cases) {
      const sm = new PetStateMachine({ battleState: state });
      expect(seq(sm, sm.setForm('battle'))).toEqual([...expected]);
      expect(sm.busy).toBe(true);
    }
  });

  it('leaves battle with the right die animation per state', () => {
    const cases = [
      ['default', 'die'],
      ['skill1', 'skill1Die'],
      ['skill2', 'skill2Die'],
      ['skill3', 'skill3Die'],
    ] as const;
    for (const [state, die] of cases) {
      const sm = new PetStateMachine({ form: 'battle', battleState: state });
      expect(seq(sm, sm.setForm('base'))).toEqual([`battle:${die}`, 'base:standIdle:loop']);
    }
  });

  it('chains exit and entry animations when switching battle states', () => {
    const sm = new PetStateMachine({ form: 'battle', battleState: 'skill1' });
    expect(seq(sm, sm.setBattleState('skill2'))).toEqual([
      'battle:skill1End',
      'battle:start2Begin',
      'battle:skill2Idle:loop',
    ]);
    expect(seq(sm, sm.setBattleState('default'))).toEqual([
      'battle:skill2End',
      'battle:idle:loop',
    ]);
    expect(seq(sm, sm.setBattleState('skill3'))).toEqual([
      'battle:start3Begin',
      'battle:skill3Idle:loop',
    ]);
  });

  it('cycles battle states default -> skill1 -> skill2 -> skill3 -> default', () => {
    const sm = new PetStateMachine({ form: 'battle' });
    expect(sm.cycleState().steps.at(-1)?.anim).toBe('skill1Idle');
    expect(sm.cycleState().steps.at(-1)?.anim).toBe('skill2Idle');
    expect(sm.cycleState().steps.at(-1)?.anim).toBe('skill3Idle');
    expect(sm.cycleState().steps.at(-1)?.anim).toBe('idle');
  });

  it('plays the correct click animation per battle state', () => {
    const cases = [
      ['default', 'attack'],
      ['skill1', 'skill1Loop'],
      ['skill2', 'skill2Loop'],
    ] as const;
    for (const [state, anim] of cases) {
      const sm = new PetStateMachine({ form: 'battle', battleState: state });
      expect(seq(sm, sm.click())).toEqual([`battle:${anim}`, `battle:${state === 'default' ? 'idle' : `${state}Idle`}:loop`]);
    }
    const sm = new PetStateMachine({ form: 'battle', battleState: 'skill3' });
    expect(sm.click(() => 0).steps[0].anim).toBe('skill3Loop');
    sm.markIdle();
    expect(sm.click(() => 1).steps[0].anim).toBe('skill3Combat');
  });

  it('base click interacts and returns to the current posture', () => {
    const sm = new PetStateMachine({ baseState: 'lie' });
    expect(seq(sm, sm.click())).toEqual(['base:interact', 'base:lieIdle:loop']);
  });

  it('base drag walks and returns to posture', () => {
    const sm = new PetStateMachine({ baseState: 'sit' });
    const start = sm.dragStart();
    expect(seq(sm, start)).toEqual(['base:move:loop']);
    expect(start.dragMode).toBe('move');
    expect(seq(sm, sm.dragEnd())).toEqual(['base:sitIdle:loop']);
  });

  it('battle default/skill1 drag neither moves nor animates', () => {
    for (const state of ['default', 'skill1'] as const) {
      const sm = new PetStateMachine({ form: 'battle', battleState: state });
      const plan = sm.dragStart();
      expect(plan.dragMode).toBe('none');
      expect(plan.steps.at(-1)?.loop).toBe(true);
    }
  });

  it('skill2 drag uses takeoff begin/loop/end and lands into skill2 begin', () => {
    const sm = new PetStateMachine({ form: 'battle', battleState: 'skill2' });
    const start = sm.dragStart();
    expect(seq(sm, start)).toEqual(['battle:skill2TakeoffBegin', 'battle:skill2TakeoffLoop:loop']);
    expect(start.dragMode).toBe('move');
    expect(seq(sm, sm.dragEnd())).toEqual([
      'battle:skill2TakeoffEnd',
      'battle:skill2Begin',
      'battle:skill2Idle:loop',
    ]);
  });

  it('skill3 drag uses Skill_3_Move and returns to idle', () => {
    const sm = new PetStateMachine({ form: 'battle', battleState: 'skill3' });
    expect(seq(sm, sm.dragStart())).toEqual(['battle:skill3Move:loop']);
    expect(seq(sm, sm.dragEnd())).toEqual(['battle:skill3Idle:loop']);
  });

  it('ignores clicks while busy', () => {
    const sm = new PetStateMachine({ form: 'battle', battleState: 'default' });
    sm.busy = true;
    expect(seq(sm, sm.click())).toEqual(['battle:idle:loop']);
  });
});
