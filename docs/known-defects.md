# 已知渲染缺陷：coconut 系列模型手部水平拉伸与畸变

## 发现与更新日期
- 首次记录：2026-10-06
- 深度排查与根因确诊：2026-10-07（详见 [docs/coconut-hand-stretch-defect-analysis.md](coconut-hand-stretch-defect-analysis.md)）

## 问题描述与真实复现条件

1. **旧记录“coconut-casual 93% 差异”结论纠偏**：
   - 经实测证实，原记录中的“93% 差异”为测试工具缺陷造成的假象：`nekomote_player --assets assets` 会默认加载背景图，而原 WebGL `capture.py` 输出透明通道，导致整图被判为大色差。
   - 在相同透明受控条件下，`coconut-casual`（单手叉腰、单手下垂）各参数下与 WebGL 均保持 **0 像素超差，完全一致**。
2. **真实受影响模型与严重 BUG 现象**：
   - 受影响模型：**双手叠放于腹部/围裙前方**的型号（`coconut-maid`, `coconut-maid-a`, `coconut-maid-a-4yo`, `coconut-santa`, `coconut-wintermaid` 等）。
   - 触发条件：身体参数发生平移或扭转（如 `body_LR: 30` 或 `body_UD: 30`）。
   - **多模态视觉现象**：
     - **WebGL 参考画面**：双手自然交叠垂放在围裙前，手指纤细并拢，比例自然。
     - **NekoMote 原生端**：右手手指发生**剧烈畸变拉伸**，被拉长 2~3 倍延展为长达 150 像素的**尖锐外星异形利爪**！
     - **实测几何误差**：最大顶点误差高达 **158.91 像素**，超差像素多达 14,044 像素。

## 确诊的根本原因

1. **`■腕R_親変形無視` 架构**：
   - 上述模型的手部为独立追加图层（`追加パーツ`），挂载在 `■腕R_親変形無視`（`inheritMask = 0x7FC`，`inheritShape = 0`）参考节点下。
   - `inheritShape == 0` 的核心语义是“断开并忽略父级节点的网格形状形变”。
2. **代码缺陷位置：`src/runtime/geometry_scene.cpp`**：
   - 历史提交 `030512d` 仅将 `inherit_shape` 用于 `mesh_path`（网格细分判定）。
   - 在构建贝塞尔网格回溯链 `curve_parent` 时，**遗漏了对 `inherit_shape` 的检查**。
   - 导致手部在执行 `deform()` 顶点求值时，一路沿着 `curve_parent` 回溯到父级手臂与躯干的大旋转变形网格（`QLR_M`），手部顶点被强行进行了第二次大幅度扭曲撕扯，导致手指暴拉 158 像素。

## 修复验证状态

- **原理验证通过**：
  在 `geometry_scene.cpp` 中阻断 `!n.inherit_shape` 节点的 `curve_parent` 回溯后：
  - 最大几何误差从 **158.91px 骤降至 8.87px**（下降 94.4%）。
  - 视觉上**异形怪爪彻底消除**，手指恢复自然收拢叠放于围裙上的正常形态。
- **详细技术分析与未确定事项**：
  完整分析见技术报告 [docs/coconut-hand-stretch-defect-analysis.md](coconut-hand-stretch-defect-analysis.md)。

## 附：coconut-dress 纹理空白问题（已修复）
coconut-dress 的 resx.json 引用的 `coconut-dress-resources/tex-texture.png` 是全空白纹理（32KB，0% 非空像素）。从 `data/coconut-dress.pure.psb.zip` 用 PsbDecompile 解包后获得的正确纹理为 3.3MB（42% 非空像素）。已用正确纹理重新导入。
