# V15 触摸视觉证据审阅报告

**Agent**: Antigravity | **Model**: Gemini 3.8 Flash | **Base**: f7f94de | **Date**: 2026-10-09  
**核查对象**: G24 触摸证据 `build/evidence/g24-touch/`（15 clean + 15 overlay Qt/FBO 图及冻结清单）  
**证据交付**: `build/evidence/v15-touch-audit/version3_observations.json`（符合 version3 标准 schema）

---

## 1. 核查范围与证据前置

消费 G24 冻结证据（`fraise-france-maid-b`、`cinnamon-maid-b`、`coconut-santa-b` 3 模型 × 5 条件）：
- **5 组 Qt/FBO 真实捕获条件**: `default`、`idle120`、`moved`（originX=80）、`scaled_half`（scale=0.5）、`lower_body_in_frame`（originY=-200）。
- **来源与绑定**: `source-manifest.json` 逐字段与 session MATCH；42 点 CPU 绑定 100% 验证通过；36 项契约检查全绿。
- **画幅排除**: `lower_body_in_frame` 在 800×1080 下身完整入画，彻底排除历史 GLFW 1600 裁切误判。

---

## 2. 逐图目视审阅结果（真眼审核）

审阅全部 15 张 clean PNG 与 15 张 overlay PNG，观察到以下关键视觉事实：

1. **Overlay 椭圆纵向映射偏置（约 540px 窗口半高偏移）**:
   - 在 `default` 与 `idle120`（originY=400）下：
     - `eye` 与 `face` 椭圆悬浮于角色头顶正上方画幅空白处（Y ≈ 16）；
     - `bust` 椭圆直接覆盖在角色刘海与额头位置（Y ≈ 357）；
     - `pant` 椭圆落在胸前衣领与胸口位置（Y ≈ 1005）。
   - 在 `lower_body_in_frame`（originY=-200）下：
     - 角色上移后下身（裙摆、围裙、短裤）完整清晰可见；
     - 但 `pant` 椭圆落在胸腹腰际线附近（Y ≈ 405），未贴合下身盆骨或短裤部位。
   - 在 `moved` 与 `scaled_half` 下：
     - 横向位移与缩放正确跟随角色，但上述纵向偏置依然存在。
   - **根因发现**: 经核查生成脚本 `generate_overlay_images.py` 中 `cy = int(round(projected_center[1] + PNG_HEIGHT / 2 - 540))` 出现 `540 - 540 == 0`，导致绘制椭圆时丢失了 Framebuffer 视口中心的 540px 偏置，使所有触摸区域视觉投影系统性上移。
2. **肢体遮挡与解剖特征**:
   - **Coconut-santa-b**: 右前臂交叉抬起搭在胸前，对手腹部区域形成明确的物理肢体遮挡。
   - **Cinnamon-maid-b**: 立绘胸部有一定侧倾偏角，与刚性垂直椭圆存在轮廓不对称。
   - **Fraise-france-maid-b**: 待机姿态对称，双手交叠于腹前，下身裙摆完整。

---

## 3. 判定与账本状态

1. **视觉对齐判定**: 由于当前生成的 overlay 投影椭圆整体上移 ~540px，未准确贴合角色真实解剖器官，3 模型 5 条件全部真实记录为 **`MISMATCH`**。
2. **候选数据处理**: 候选触摸数据保持 **`PENDING_EVIDENCE`**，严禁凭 CPU 成功或椭圆数学伪造 MATCH，严禁按家族外推。
3. **格式合规**: 已生成标准 `build/evidence/v15-touch-audit/version3_observations.json`，包含 `version: 3`、`observer`、`notes`、`conditions`、`checked_zones` 以及逐条件 `clean` / `overlay` / `source` / `model_asset` / `session_asset` 完整路径与 SHA-256。

---

## 结论

G24 真实 Qt/FBO 下半身画幅成功进入视野，排除了裁切假象；但 overlay 投影脚本存在 540px 纵向偏置，视觉呈现暂未贴合解剖部位。version3 观察记录已如实冻结，候选数据继续保持 PENDING。
