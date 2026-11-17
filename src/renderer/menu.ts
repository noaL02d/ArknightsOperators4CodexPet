import { BASE_STATE_LABELS, BATTLE_STATE_LABELS, type MenuState } from '../shared/types';

const root = document.getElementById('menu') as HTMLDivElement;
let state: MenuState | null = null;

function el<K extends keyof HTMLElementTagNameMap>(
  tag: K,
  className = '',
  text?: string,
): HTMLElementTagNameMap[K] {
  const node = document.createElement(tag);
  if (className) node.className = className;
  if (text !== undefined) node.textContent = text;
  return node;
}

function section(title: string): HTMLDivElement {
  const box = el('div', 'section');
  box.appendChild(el('div', 'section-title', title));
  return box;
}

function button(label: string, active: boolean, onClick: () => void): HTMLButtonElement {
  const b = el('button', `chip${active ? ' active' : ''}`, label);
  b.addEventListener('click', onClick);
  return b;
}

function rowToggle(label: string, checked: boolean, onClick: () => void): HTMLButtonElement {
  const b = el('button', `toggle${checked ? ' on' : ''}`);
  const span = el('span', '', label);
  const knob = el('span', 'knob');
  b.append(span, knob);
  b.addEventListener('click', onClick);
  return b;
}

function render(): void {
  if (!state) return;
  root.innerHTML = '';
  const header = el('div', 'header');
  const title = el('div', 'title', '予愿安洁莉娜');
  const sub = el('div', 'sub', 'Arknights · Codex Pet');
  header.append(title, sub);
  root.appendChild(header);

  const people = section('人物');
  const peopleRow = el('div', 'chips');
  for (const op of state.operators) {
    peopleRow.appendChild(
      button(op.name, op.id === state.operatorId, () =>
        window.petAPI.menuAction({ type: 'operator', value: op.id }),
      ),
    );
  }
  people.appendChild(peopleRow);
  root.appendChild(people);

  const form = section('形态');
  const formRow = el('div', 'chips');
  formRow.append(
    button('基建', state.form === 'base', () =>
      window.petAPI.menuAction({ type: 'form', value: 'base' }),
    ),
    button('战斗', state.form === 'battle', () =>
      window.petAPI.menuAction({ type: 'form', value: 'battle' }),
    ),
  );
  form.appendChild(formRow);
  root.appendChild(form);

  const stateSec = section('状态');
  const stateRow = el('div', 'chips');
  if (state.form === 'base') {
    for (const key of ['stand', 'sit', 'lie'] as const) {
      stateRow.appendChild(
        button(BASE_STATE_LABELS[key], state.baseState === key, () =>
          window.petAPI.menuAction({ type: 'baseState', value: key }),
        ),
      );
    }
  } else {
    for (const key of ['default', 'skill1', 'skill2', 'skill3'] as const) {
      stateRow.appendChild(
        button(BATTLE_STATE_LABELS[key], state.battleState === key, () =>
          window.petAPI.menuAction({ type: 'battleState', value: key }),
        ),
      );
    }
  }
  stateSec.appendChild(stateRow);
  root.appendChild(stateSec);

  const sizeSec = section(`大小 ${Math.round(state.scale * 100)}%`);
  const slider = el('input', 'slider');
  slider.type = 'range';
  slider.min = '50';
  slider.max = '200';
  slider.step = '1';
  slider.value = String(Math.round(state.scale * 100));
  slider.addEventListener('input', () => {
    void window.petAPI.setScale(Number(slider.value) / 100);
  });
  const reset = el('button', 'mini', '重置');
  reset.addEventListener('click', () => {
    void window.petAPI.resetScale();
    slider.value = '56';
  });
  const sizeRow = el('div', 'size-row');
  sizeRow.append(slider, reset);
  sizeSec.appendChild(sizeRow);
  sizeSec.appendChild(el('div', 'hint', '拖拽中滚轮缩放；中键恢复 56%'));
  root.appendChild(sizeSec);

  const session = section('会话');
  const sessionRow = el('div', 'chips vertical');
  sessionRow.appendChild(
    button('自动跟随最近会话', state.pinnedThreadId === null, () =>
      window.petAPI.menuAction({ type: 'pinThread', value: null }),
    ),
  );
  for (const thread of state.recentThreads.slice(0, 5)) {
    const label = thread.title.length > 22 ? `${thread.title.slice(0, 22)}…` : thread.title;
    sessionRow.appendChild(
      button(label, state.pinnedThreadId === thread.id, () =>
        window.petAPI.menuAction({ type: 'pinThread', value: thread.id }),
      ),
    );
  }
  session.appendChild(sessionRow);
  root.appendChild(session);

  const display = section('显示');
  display.append(
    rowToggle('信息卡', state.sessionCardVisible, () =>
      window.petAPI.menuAction({ type: 'toggleCard' }),
    ),
    rowToggle('窗口置顶', state.alwaysOnTop, () =>
      window.petAPI.menuAction({ type: 'toggleTop' }),
    ),
    rowToggle('随 Windows 启动', state.autostart, () =>
      window.petAPI.menuAction({ type: 'toggleAutostart' }),
    ),
    rowToggle('Codex 退出后关闭', state.closeWithCodex, () =>
      window.petAPI.menuAction({ type: 'toggleCloseWithCodex' }),
    ),
  );
  root.appendChild(display);

  const footer = el('div', 'footer');
  const install = el('button', 'wide', '安装 / 更新 Codex 原生宠物');
  install.addEventListener('click', () => {
    window.petAPI.menuAction({ type: 'installNativePet' });
  });
  const quit = el('button', 'wide danger', '退出');
  quit.addEventListener('click', () => window.petAPI.quit());
  footer.append(install, quit);
  root.appendChild(footer);
}

window.petAPI.onMenuState((next) => {
  state = next;
  render();
});

window.petAPI.menuState().then((next) => {
  state = next;
  render();
});

window.addEventListener('keydown', (event) => {
  if (event.key === 'Escape') window.petAPI.hideMenu();
});
