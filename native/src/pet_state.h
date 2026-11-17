#pragma once

#include <cstdlib>
#include <string>
#include <vector>

enum class Form { Base, Battle };
enum class BaseState { Stand, Sit, Lie };
enum class BattleState { Default, Skill1, Skill2, Skill3 };
enum class DragMode { None, Move, Skill3Move };

struct Step {
  Form model = Form::Base;
  std::string anim;
  bool loop = false;
};

struct PetPlan {
  std::vector<Step> steps;
  DragMode dragMode = DragMode::None;
  bool busy = false;
};

inline Step makeStep(Form model, const char* anim, bool loop = false) {
  Step step;
  step.model = model;
  step.anim = anim;
  step.loop = loop;
  return step;
}

class PetStateMachine {
 public:
  Form form = Form::Base;
  BaseState baseState = BaseState::Stand;
  BattleState battleState = BattleState::Default;
  bool busy = false;
  DragMode dragMode = DragMode::None;

  void reset(Form newForm, BaseState newBase, BattleState newBattle) {
    form = newForm;
    baseState = newBase;
    battleState = newBattle;
    busy = false;
    dragMode = DragMode::None;
  }

  Step baseIdle() const {
    return makeStep(Form::Base,
                    baseState == BaseState::Sit ? "sitIdle"
                                                : (baseState == BaseState::Lie ? "lieIdle" : "standIdle"),
                    true);
  }

  Step battleIdle() const {
    return makeStep(Form::Battle,
                    battleState == BattleState::Skill1
                        ? "skill1Idle"
                        : (battleState == BattleState::Skill2
                               ? "skill2Idle"
                               : (battleState == BattleState::Skill3 ? "skill3Idle" : "idle")),
                    true);
  }

  Step idle() const { return form == Form::Base ? baseIdle() : battleIdle(); }

  PetPlan idlePlan() { return finish({}, idle(), false, DragMode::None); }

  void markIdle() {
    busy = false;
    dragMode = DragMode::None;
  }

  PetPlan setForm(Form target) {
    if (target == form) return idlePlan();
    form = target;
    if (target == Form::Battle) {
      Step idleStep = battleIdle();
      std::vector<Step> steps;
      switch (battleState) {
        case BattleState::Skill1:
          steps.push_back(makeStep(Form::Battle, "start2"));
          break;
        case BattleState::Skill2:
          steps.push_back(makeStep(Form::Battle, "start2Begin"));
          break;
        case BattleState::Skill3:
          steps.push_back(makeStep(Form::Battle, "start"));
          steps.push_back(makeStep(Form::Battle, "restart3"));
          break;
        default:
          steps.push_back(makeStep(Form::Battle, "start"));
          break;
      }
      return finish(steps, idleStep, true, DragMode::None);
    }
    const char* die = battleState == BattleState::Skill1
                          ? "skill1Die"
                          : (battleState == BattleState::Skill2
                                 ? "skill2Die"
                                 : (battleState == BattleState::Skill3 ? "skill3Die" : "die"));
    return finish({makeStep(Form::Battle, die)}, baseIdle(), true, DragMode::None);
  }

  PetPlan toggleForm() { return setForm(form == Form::Base ? Form::Battle : Form::Base); }

  PetPlan cycleState() {
    if (form == Form::Base) {
      BaseState next = baseState == BaseState::Stand
                           ? BaseState::Sit
                           : (baseState == BaseState::Sit ? BaseState::Lie : BaseState::Stand);
      return setBaseState(next);
    }
    BattleState next = battleState == BattleState::Default
                           ? BattleState::Skill1
                           : (battleState == BattleState::Skill1
                                  ? BattleState::Skill2
                                  : (battleState == BattleState::Skill2 ? BattleState::Skill3
                                                                        : BattleState::Default));
    return setBattleState(next);
  }

  PetPlan setBaseState(BaseState next) {
    baseState = next;
    if (form == Form::Battle) form = Form::Base;
    return finish({}, baseIdle(), false, DragMode::None);
  }

  PetPlan setBattleState(BattleState next) {
    if (form == Form::Base) {
      battleState = next;
      return setForm(Form::Battle);
    }
    if (next == battleState) return idlePlan();
    BattleState leaving = battleState;
    battleState = next;
    std::vector<Step> steps;
    if (leaving == BattleState::Skill1) steps.push_back(makeStep(Form::Battle, "skill1End"));
    if (leaving == BattleState::Skill2) steps.push_back(makeStep(Form::Battle, "skill2End"));
    if (leaving == BattleState::Skill3) steps.push_back(makeStep(Form::Battle, "skill3End"));
    if (next == BattleState::Skill1) steps.push_back(makeStep(Form::Battle, "start2"));
    if (next == BattleState::Skill2) steps.push_back(makeStep(Form::Battle, "start2Begin"));
    if (next == BattleState::Skill3) steps.push_back(makeStep(Form::Battle, "start3Begin"));
    if (steps.empty()) return finish({}, battleIdle(), false, DragMode::None);
    return finish(steps, battleIdle(), true, DragMode::None);
  }

  PetPlan click() {
    if (busy) return idlePlan();
    if (form == Form::Base) {
      return finish({makeStep(Form::Base, "interact")}, baseIdle(), true, DragMode::None);
    }
    const char* anim = "attack";
    if (battleState == BattleState::Skill1) anim = "skill1Loop";
    if (battleState == BattleState::Skill2) anim = "skill2Loop";
    if (battleState == BattleState::Skill3) anim = (std::rand() % 2 == 0) ? "skill3Loop" : "skill3Combat";
    return finish({makeStep(Form::Battle, anim)}, battleIdle(), true, DragMode::None);
  }

  PetPlan dragStart() {
    if (form == Form::Base) {
      return finish({}, makeStep(Form::Base, "move", true), false, DragMode::Move);
    }
    if (battleState == BattleState::Skill1 || battleState == BattleState::Default) {
      return finish({}, battleIdle(), false, DragMode::None);
    }
    if (battleState == BattleState::Skill2) {
      return finish({makeStep(Form::Battle, "skill2TakeoffBegin")},
                    makeStep(Form::Battle, "skill2TakeoffLoop", true), false, DragMode::Move);
    }
    return finish({}, makeStep(Form::Battle, "skill3Move", true), false, DragMode::Skill3Move);
  }

  PetPlan dragEnd() {
    if (form == Form::Base) return finish({}, baseIdle(), false, DragMode::None);
    if (battleState == BattleState::Skill2) {
      return finish({makeStep(Form::Battle, "skill2TakeoffEnd"),
                     makeStep(Form::Battle, "skill2Begin")},
                    battleIdle(), true, DragMode::None);
    }
    return finish({}, battleIdle(), false, DragMode::None);
  }

 private:
  PetPlan finish(std::vector<Step> steps, Step idleStep, bool isBusy, DragMode mode) {
    steps.push_back(idleStep);
    busy = isBusy;
    dragMode = mode;
    PetPlan plan;
    plan.steps = std::move(steps);
    plan.dragMode = mode;
    plan.busy = isBusy;
    return plan;
  }
};
