# ArknightsOperators4CodexPet

把明日方舟干员小人做成 Codex 桌面宠物。当前内置：**予愿安洁莉娜**。

从 v1.0.0 开始使用 **MinGW C++ + Direct3D 11 + 官方 spine-cpp 3.8** 原生实现：
单进程、60fps、约 65 MB 内存。重构前的 Electron 版本完整保留在
[`v0.1.0`](https://github.com/noaL02d/ArknightsOperators4CodexPet/releases/tag/v0.1.0) 标签与 Release 中。

## 特性

- Spine 3.8.99 骨骼动画，基建 / 战斗两套模型常驻
- 基建：站 / 坐 / 躺；战斗：默认 / 技能一 / 技能二 / 技能三，共 26 个动画
- `ClippingAttachment` 裁剪：眨眼时眼球被眼皮裁掉，武器遮罩同样生效
- DirectComposition 合成，逐像素透明，透明处点击穿透
- 60fps（帧时间约 0.55 ms），窗口隐藏时完全停帧
- 托盘菜单、开机启动、单实例、配置持久化、Per-Monitor V2 DPI
- 只在 `ChatGPT.exe` 运行时显示

## 操作

| 操作 | 基建形态 | 战斗形态 |
|---|---|---|
| 鼠标前侧键 | 切换形态 | 切换形态 |
| 鼠标后侧键 | 站 / 坐 / 躺 | 默认 / 技能一 / 技能二 / 技能三 |
| 左键单击 | 交互动画 | 攻击 / 技能循环 |
| 左键双击 | 播放单击动画并打开 Codex | 同左 |
| 左键拖动 | 走路并移动窗口 | 技能二 / 三可移动；默认 / 技能一无移动 |
| 拖拽中滚轮 | 缩放，鼠标下的角色位置保持不动 | 同左 |
| 拖拽中按中键 | 大小恢复 56% | 同左 |
| 右键 | 打开菜单（形态、状态、大小、置顶、开机启动、退出） | 同左 |

## 运行

```powershell
native\start.cmd
```

或直接运行 `native\build\ArknightsPet.exe`。便携版见 Release 中的
`ArknightsPet-1.0.0-win64.zip`：解压后 `assets` 目录与 exe 同级即可运行。

## 构建

需要 MinGW-w64（g++ 支持 C++17）和 windres。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File native/build.ps1
```

产物：`native/build/ArknightsPet.exe`（约 1.5 MB）以及 `native/build/assets`。
加 `-Console` 会保留控制台日志，便于调试。

## 性能

测试环境：2560×1600 @150%，60fps。

| | Electron v0.1.0 | 原生 v1.0.0（320×315） | 原生 v1.0.0（628×619） |
|---|---:|---:|---:|
| 进程数 | 6 | 1 | 1 |
| 私有内存 | 395 MB | 65 MB | 65 MB |
| CPU（单核占比） | 45% | 3.5% | 4.9% |
| 帧时间 | 16 ms | 0.55 ms | 0.80 ms |

## 项目结构

```
native/             C++ 源码
  src/main.cpp        窗口、输入、菜单、托盘、帧循环
  src/spine_player.*  Spine 加载、状态机播放、裁剪渲染
  src/renderer.*      D3D11 + DirectComposition
  src/pet_state.h     形态 / 状态机（与旧版逐条对齐）
  build.ps1           构建脚本
public/assets/      干员资源（Spine 模型、贴图、pet.json）
public/vendor/      spine-arknights.js（参考运行时）与许可证
tools/spine-preview 用官方运行时逐帧渲染，便于和原生版对比
scripts/fetch-asset.mjs  下载新干员资源
```

## 调试

```powershell
# 离屏渲染单帧
native\build\ArknightsPet.exe --snapshot out.png --form battle --battle skill3 --scale 1.6 --frames 78

# 打印所有状态转移对应的动画序列
native\build\ArknightsPet.exe --selftest

# 打印某个动画的槽位 / 附件 / 包围盒变化
native\build\ArknightsPet.exe --dump standIdle

# 用官方 JS 运行时渲染同一帧做对比
npx electron tools/spine-preview/main.js Relax 2.6 out.png base 1.6
```

## 资源与新增干员

```powershell
node scripts/fetch-asset.mjs --id char_1015_aglna2
```

资源来自 PRTS 的公开资产存储，映射在 `public/assets/operators/<id>/pet.json`。

## 声明

《明日方舟》角色、模型、图片版权属于上海鹰角网络科技有限公司及其关联公司。
本项目是非商业粉丝工具，仅用于学习与桌宠展示。
Spine 运行时版权与许可见 `public/vendor/SPINE-RUNTIMES-LICENSE.txt`。
