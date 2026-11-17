import type {
  BaseState,
  BattleState,
  DragMode,
  Form,
  PetPlan,
  Step,
} from './types';

const step = (model: Form, anim: string, loop = false): Step => ({ model, anim, loop });

export class PetStateMachine {
  form: Form = 'base';
  baseState: BaseState = 'stand';
  battleState: BattleState = 'default';
  busy = false;
  dragMode: DragMode = 'none';

  constructor(init?: Partial<Pick<PetStateMachine, 'form' | 'baseState' | 'battleState'>>) {
    if (init?.form) this.form = init.form;
    if (init?.baseState) this.baseState = init.baseState;
    if (init?.battleState) this.battleState = init.battleState;
  }

  baseIdle(): Step {
    const anim =
      this.baseState === 'sit' ? 'sitIdle' : this.baseState === 'lie' ? 'lieIdle' : 'standIdle';
    return step('base', anim, true);
  }

  battleIdle(): Step {
    const anim =
      this.battleState === 'skill1'
        ? 'skill1Idle'
        : this.battleState === 'skill2'
          ? 'skill2Idle'
          : this.battleState === 'skill3'
            ? 'skill3Idle'
            : 'idle';
    return step('battle', anim, true);
  }

  idle(): Step {
    return this.form === 'base' ? this.baseIdle() : this.battleIdle();
  }

  private finish(steps: Step[], idle: Step, busy: boolean, dragMode: DragMode = 'none'): PetPlan {
    this.busy = busy;
    this.dragMode = dragMode;
    return { steps: [...steps, idle], dragMode, busy };
  }

  idlePlan(): PetPlan {
    return this.finish([], this.idle(), false, 'none');
  }

  markIdle(): void {
    this.busy = false;
    this.dragMode = 'none';
  }

  setForm(target: Form): PetPlan {
    if (target === this.form) return this.idlePlan();
    this.form = target;
    if (target === 'battle') {
      const idle = this.battleIdle();
      let steps: Step[];
      switch (this.battleState) {
        case 'skill1':
          steps = [step('battle', 'start2')];
          break;
        case 'skill2':
          steps = [step('battle', 'start2Begin')];
          break;
        case 'skill3':
          steps = [step('battle', 'start'), step('battle', 'restart3')];
          break;
        default:
          steps = [step('battle', 'start')];
      }
      return this.finish(steps, idle, true);
    }
    const idle = this.baseIdle();
    const die =
      this.battleState === 'skill1'
        ? 'skill1Die'
        : this.battleState === 'skill2'
          ? 'skill2Die'
          : this.battleState === 'skill3'
            ? 'skill3Die'
            : 'die';
    return this.finish([step('battle', die)], idle, true);
  }

  toggleForm(): PetPlan {
    return this.setForm(this.form === 'base' ? 'battle' : 'base');
  }

  cycleState(): PetPlan {
    if (this.form === 'base') {
      const next: BaseState =
        this.baseState === 'stand' ? 'sit' : this.baseState === 'sit' ? 'lie' : 'stand';
      return this.setBaseState(next);
    }
    const next: BattleState =
      this.battleState === 'default'
        ? 'skill1'
        : this.battleState === 'skill1'
          ? 'skill2'
          : this.battleState === 'skill2'
            ? 'skill3'
            : 'default';
    return this.setBattleState(next);
  }

  setBaseState(next: BaseState): PetPlan {
    this.baseState = next;
    if (this.form === 'battle') this.form = 'base';
    return this.finish([], this.baseIdle(), false);
  }

  setBattleState(next: BattleState): PetPlan {
    if (this.form === 'base') {
      this.battleState = next;
      return this.setForm('battle');
    }
    if (next === this.battleState) return this.idlePlan();
    const leaving = this.battleState;
    this.battleState = next;
    const steps: Step[] = [];
    if (leaving === 'skill1') steps.push(step('battle', 'skill1End'));
    if (leaving === 'skill2') steps.push(step('battle', 'skill2End'));
    if (leaving === 'skill3') steps.push(step('battle', 'skill3End'));
    if (next === 'skill1') steps.push(step('battle', 'start2'));
    if (next === 'skill2') steps.push(step('battle', 'start2Begin'));
    if (next === 'skill3') steps.push(step('battle', 'start3Begin'));
    if (steps.length === 0) return this.finish([], this.battleIdle(), false);
    return this.finish(steps, this.battleIdle(), true);
  }

  click(random = Math.random): PetPlan {
    if (this.busy) return this.idlePlan();
    if (this.form === 'base') {
      return this.finish([step('base', 'interact')], this.baseIdle(), true);
    }
    let anim = 'attack';
    if (this.battleState === 'skill1') anim = 'skill1Loop';
    if (this.battleState === 'skill2') anim = 'skill2Loop';
    if (this.battleState === 'skill3') anim = random() < 0.5 ? 'skill3Loop' : 'skill3Combat';
    return this.finish([step('battle', anim)], this.battleIdle(), true);
  }

  dragStart(): PetPlan {
    if (this.form === 'base') {
      const move = step('base', 'move', true);
      return this.finish([], move, false, 'move');
    }
    if (this.battleState === 'skill1' || this.battleState === 'default') {
      return this.finish([], this.battleIdle(), false, 'none');
    }
    if (this.battleState === 'skill2') {
      const loop = step('battle', 'skill2TakeoffLoop', true);
      return this.finish([step('battle', 'skill2TakeoffBegin')], loop, false, 'move');
    }
    const move = step('battle', 'skill3Move', true);
    return this.finish([], move, false, 'skill3move');
  }

  dragEnd(): PetPlan {
    if (this.form === 'base') {
      return this.finish([], this.baseIdle(), false);
    }
    if (this.battleState === 'skill2') {
      return this.finish(
        [step('battle', 'skill2TakeoffEnd'), step('battle', 'skill2Begin')],
        this.battleIdle(),
        true,
      );
    }
    return this.finish([], this.battleIdle(), false);
  }
}
