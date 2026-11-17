# 原生版桌宠（MinGW C++）

用 MinGW-w64 + Direct3D 11 + 官方 spine-cpp 3.8 重写的桌宠外壳。单进程、单线程渲染，不再依赖 Electron / Chromium。

## 构建

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File native/build.ps1
```

产物：`native/build/ArknightsPet.exe`（约 1.4 MB）。调试时可加 `-Console`，会保留控制台日志。

运行：双击 `native/start.cmd`，或直接执行 exe。

## 实现对照

| 模块 | 旧实现 | 新实现 |
|---|---|---|
| 窗口 | Electron 3 个 BrowserWindow | 1 个 Win32 窗口 + DirectComposition 合成视觉（预乘 alpha） |
| 渲染 | Spine WebGL + Chromium 合成 | spine-cpp 3.8 求顶点，D3D11 一次批量绘制 |
| 每帧 | 60fps 无条件重绘 | 60fps，窗口隐藏时完全停帧；无 GPU 回读 |
| 进程检测 | 每 2.5s 起一个 `tasklist.exe` | 每 2s `CreateToolhelp32Snapshot`（进程内，微秒级） |
| 读取 Codex 状态 | 每 2s 起一个 Electron 子进程读 SQLite | 待迁移（见下） |
| 交互 | JS 事件 + IPC | 原生鼠标消息 + alpha 命中测试 |

## 已实现

- Spine 3.8.99 模型加载（基建 / 战斗两套模型常驻，约 1.4 MB 纹理）
- 动画裁剪（`ClippingAttachment`）：眨眼时眼球会被眼皮多边形裁掉，武器遮罩同样生效
- 状态机：站/坐/躺、默认/技能一/二/三、点击交互动画、拖拽移动/朝向、技能二/三的移动形态
- 左键拖动移动、拖动时滚轮缩放（以鼠标为锚点）、中键恢复 56%
- 鼠标前侧键切换形态，后侧键循环状态，右键菜单
- 托盘图标 + 原生菜单（形态、状态、大小、置顶、开机启动、退出）
- 透明区域点击穿透（按 alpha 判断）、多显示器坐标钳制、Per-Monitor V2 DPI
- 配置持久化：`%APPDATA%\ArknightsOperators4CodexPet\config.json`
- 只在 `ChatGPT.exe` 运行时显示
- `--snapshot <out.png> [--form battle] [--battle skill3] [--base sit] [--scale 1.0]` 离屏渲染单帧
- `--selftest` 打印状态机每条转移对应的动画序列，便于和策划表逐条核对

## 待迁移

- Codex 会话监视（`~/.codex/sessions` 增量 tail + `session_index.jsonl`）
- 状态圆点（蓝/绿/灰）与会话卡片窗口
- 原生宠物包导出（codex-pet spritesheet）

## 渲染对齐说明

贴图和混合方式跟 `public/vendor/spine-arknights.js` 保持一致：纹理按直通 alpha 上传，
片元着色器输出 `texel * vertexColor`，混合用 `ONE / ONE_MINUS_SRC_ALPHA`（叠加槽用 `ONE / ONE`）。
不要改成预乘纹理，否则眼睛高光和技能发光会明显偏暗。

裁剪用自己实现的凸多边形 Sutherland–Hodgman，没有用 spine-cpp 3.8 自带的 `SkeletonClipping`：
它的 `Triangulator` 在 MinGW 下会访问越界（同一套数据官方 JS 运行时正常）。

## 调试工具

- `tools/spine-preview`：用官方 `spine-arknights.js` 渲染指定帧，用来逐帧比对
  `electron tools/spine-preview/main.js Relax 2.6 out.png base 1.6 610 602 [only正则] [hide正则]`
- `--dump <状态>`：打印某状态的槽位/附件/包围盒变化

## 实测（本机 2560×1600 @150%，30fps）

| | Electron 版 | 原生版 |
|---|---:|---:|
| 私有内存 | 395 MB（6 进程） | 68.6 MB（1 进程） |
| CPU（单核占比） | 45% | 3.8% |

改用 DirectComposition 并提到 60fps 后（窗口 320×315 / 628×619）：

| | 320×315 | 628×619 |
|---|---:|---:|
| 帧时间 | 0.55 ms | 0.80 ms |
| CPU（单核占比） | 3.5% | 4.9% |
| 私有内存 | 65 MB | 65 MB |

Spine 运行时版权与许可见 `public/vendor/SPINE-RUNTIMES-LICENSE.txt`。
