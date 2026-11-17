# ArknightsOperators4CodexPet

把明日方舟干员小人做成 Codex 桌面宠物。当前内置：**予愿安洁莉娜**。

## 运行

已安装依赖时，双击 `start-pet.cmd`，或：

```powershell
.\node_modules\electron\dist\electron.exe .
```

便携版：`release\ArknightsOperators4CodexPet-0.1.0.exe`（重新打包后才是最新修复）。

开发/重新构建：

```powershell
pnpm install
pnpm dev
```

宠物只在 Codex（`ChatGPT.exe`）运行时显示；托盘可显示/隐藏、设置随 Windows 启动或退出。

## 操作

| 操作 | 基建形态 | 战斗形态 |
|---|---|---|
| 鼠标前侧键 | 切换形态 | 切换形态 |
| 鼠标后侧键 | 站/坐/躺 | 默认/技能一/技能二/技能三 |
| 左键单击 | 交互动画 | 攻击 / 技能循环 |
| 左键双击 | 播放单击动画并打开 Codex 当前会话 | 同左 |
| 左键拖动 | 走路并移动窗口 | 技能二/三可移动；默认/技能一无移动 |
| 拖拽中滚轮 | 缩放，鼠标下的角色位置保持不动 | 同左 |
| 拖拽中按中键 | 大小恢复 56% | 同左 |
| 右键 | 打开菜单（人物、形态、状态、大小、会话） | 同左 |

缩放手感：拖拽本身不会改变大小；拖拽中滚轮缩放，中键恢复 56%，菜单滑条也可调整。

## Codex 集成

- 自动读取 `~/.codex/sessions` 和 `session_index.jsonl`，按会话文件活跃度定位当前会话。
- 会话卡显示标题、运行状态、计划步骤和待选问题；点击“打开 Codex”跳转到对应会话。
- 菜单 → `安装 / 更新 Codex 原生宠物` 会生成并安装到 `~/.codex/pets/yu-yuan-anjielina/`；然后在 Codex 设置 → 个性化 → Mini & Pets 点 Refresh 选择。

## 资源与新增干员

```powershell
pnpm fetch-asset --id char_1015_aglna2
```

资源来自 PRTS 的公开资产存储，映射在 `public/assets/operators/<id>/pet.json`。新增干员需要提供同结构的清单。

## 声明

《明日方舟》角色、模型、图片版权属于上海鹰角网络科技有限公司及其关联公司。本项目是非商业粉丝工具，仅用于学习与桌宠展示。Spine 运行时版权与许可见 `public/vendor/SPINE-RUNTIMES-LICENSE.txt`。
