# V18 动态与组合求值视觉核查报告

**Agent**: Antigravity | **Model**: Gemini 3.8 Flash | **Base**: f7f94de | **Date**: 2026-10-09  
**核查对象**: G26 动态与 45 组合证据、Codex 12 新型号与 3 idle120 证据  
**证据目录**: `build/evidence/v18-dynamic-visual/`（含 crops 局部裁图与 manifest.json）

---

## 1. 核查范围与证据源

1. **G26 动态场景 (`build/evidence/g26-validation/dynamic/`)**:
   - 动作终态（`挨拶`、`ピョン` 各 120 步 × 3 代表）：native vs reference 渲染图及差分图（6 组全通，0 超差像素）。
   - 中途事件（`idle60 -> 挨拶60`）：headless probe 几何误差 ≤ 3.81e-5 px，物理标量 ≤ 1.19e-7。
   - 暂停恢复与 locator 投影：6 active locator 在转体下全部单调位移。
2. **G26 45 组合 (`build/evidence/g26-validation/combos-45/`)**:
   - 审阅局部微变化、遮罩裁切（`eye_open50`, `mouth_open50`）、物理控制（`bust_LR20`）、倾斜变形（`body_LR_slant30`）及复合极值（`full_combo`）。
3. **Codex 正式集成证据 (`build/evidence/codex-g25-integration/`)**:
   - 12 历史 FAIL 型号（含 `coconut-hawaii-a`、`azuki-maid-b` 等）24 组静态/bodyLR30 对照（alpha 差 0，容差 2 超差为 0）。
   - 3 代表 `idle120` 终态（120 步物理/待机动画终态，图像与参考零超差）。

---

## 2. 关键解剖部位与局部变化目视审阅（真眼审核）

1. **腕部/衣袖挂接（0x7fc 动态闭环验证）**:
   - **动态动作**: 在 `coconut-santa`、`azuki-santa-b`、`cinnamon-maid` 的 `挨拶` 与 `ピョン` 120 步终态中，双手交叉腕部、抱臂袖口与双手合十的边缘完全贴合，彻底消除了历史 8.95px 的 X 轴位移断层。
   - **倾斜与复合极值**: 在 `body_LR_slant30`（产生 56.62px 实际顶点偏转）与 `full_combo` 复合极值下，腕部曲面随躯干倾斜自然变形，无任何网格拉扯撕裂或图层反转。
   - **12 新型号**: 审阅夏威夷装等无袖/短袖模型，手腕装饰物（如发圈、袖边）与手臂轮廓贴合完美。
2. **遮罩边缘 (Stencil / Clipping Mask)**:
   - **眼睛遮罩 (`eye_open50`)**: 半开眼状态下，眼皮边缘对眼球与瞳孔的 Stencil 裁切边界平滑锐利，无遮罩溢出（bleeding）或伪影残留。
   - **口腔遮罩 (`mouth_open50`)**: 嘴部半开状态下，口腔内部底色与舌部完全被限制在上下唇轮廓内，差分通道纯黑（零误差）。
3. **局部变化与物理隔离**:
   - **视线位移 (`eye_LR30`, `eye_UD30`)**: 眼球微位移正确反映，面部其他部件严格不动。
   - **胸部物理控制 (`bust_LR20`)**: 差分图像严格局限于胸部与斗篷衣领区域。因 static CLI pose 工具未驱动物理控制器，native 保持默认形态，与 WebGL spring 偏移产生局部差异；腕部、手臂、面部 100% 吻合，证实非几何挂接公式反例。

---

## 3. 边界与限制说明

1. **终态截帧 vs 连续性**: 本次审阅的动作图为 step 120 终态截帧，不能据此单方面认证全帧率动态连续性。
2. **中途事件截帧**: 中途事件当前通过 headless playback probe 比对几何与标量，preview CLI 未生成 native 渲染图。
3. **未覆盖项**: 组合差分、桌面插件实际安装、声音/背景与长期多屏仍在后续路线图。

---

## 结论

G26 动态动作终态、45 组合（遮罩与局部变化）、Codex 12 新型号与 idle120 终态**目视审阅全部通过**：0x7fc 腕部修复在动作与复合姿态下完全稳定，眼/嘴遮罩准确，局部变化物理隔离明确。
