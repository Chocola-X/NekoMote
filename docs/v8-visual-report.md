# V8 镜像与遮罩独立复核报告

- **日期**：2026-10-08
- **执行者**：Antigravity
- **模型**：Gemini 3.8 Flash (High)
- **任务上下文**：`docs/agent-workboard.md` V8
- **基线提交**：`1343172`（在 C5 `998a582` 及 G11 `483a3ff`、`1343172` 之后）
- **证据目录**：`build/evidence/v8/`
- **机器账本**：`build/evidence/v8/v8_ledger.json`

---

## 1. 复核目标与验收门槛

本任务针对 Codex C5 提交的 **schema 6 镜像参数展开**、**可选遮罩源优雅降级**、**motion 裁剪组独立隔离** 及 **呈现镜像物理锚点**，执行独立、跨模型的视觉、几何、拓扑与标量复核。

所有测试遵循以下刚性约束：
1. **直接作者输入**：原生端直接传入模型原始定义的作者变量（如 `head_LR = 30`、`head_slant = 30`），**不进行手工取反**，验证运行时是否自动完成一次性反转。
2. **正式呈现镜像**：模型资产保持正式 `asset.mirror = true`（对镜像角色），输出遵循请求的水平放置。
3. **隔离与串行**：全程受 `flock -n build/gpu-validation.lock` 保护，无其他 GPU 任务并发；不向系统注入任何键鼠事件。
4. **统一验收门槛**：
   - 几何误差：$\le 0.001\text{px}$
   - 拓扑一致性：全部匹配（0 丢失图元、0 丢失参考三角形、0 未匹配参考顶点、0 未使用原生顶点/三角形、0 帧错误）
   - 标量公差：$\le 1.0\times 10^{-5}$（反例严格如实记录 FAIL）
   - 图像像素差：alpha 误差与黑白底合成 RGB 误差 $\le 2/255$，且**超差像素数为 0**（`pixels_over_tolerance == 0`）。

---

## 2. 工具、二进制与资产哈希审计

为确保复核环境与证据链可追溯，测试前对执行二进制及模型资产计算 SHA-256：

| 目标 | 路径 | SHA-256 |
|---|---|---|
| 预览工具 | `build/release/bin/nekomote_preview` | `108b6b44f70e4653170993f72805b516a03f38c37e454dc64e0c31fac31d26cc` |
| 静态姿态探针 | `build/release/bin/nekomote_pose` | `1f5acbf3b16d61c2b2ca9087602b2f42ed92a8fca407187d830c71e5af49427b` |
| 动画回放探针 | `build/release/bin/nekomote_playback_probe` | `a0fca91dd27ad0eeede28d187a9097cfeca8424284379b71c682bcbca2b3db54` |
| 离线帧渲染器 | `build/release/bin/nekomote_frame_render` | `6ffbae4a7066f173ddb9bf87ace384b74c7bf22eacc9b79f77e0a4267a1ab386` |
| 资产（镜像+可选遮罩） | `assets/characters/fraise-france-maid-b/model.nkm.json` | `2f85c928364d656596be0d48ac35af34bfab27e3caff7bc4b0053bcdf7fab2d5` |
| 资产（动作裁剪组） | `assets/characters/chocola-date-a/model.nkm.json` | `eddc226dd2056ccf96ab8cf179824386f427d2db9b20f6420689c316a0368690` |
| 资产（真实 add_mask） | `assets/characters/fraise-maid-a/model.nkm.json` | `fde6c41dff5812df937afd2da86c5c702363891a93289feb9fc7220ad654095c` |

全部 3 个资产均为 schema 6。`fraise-france-maid-b` 展开了 24 个 `mirrored_parameters`（涵盖 `_LR` 与 `_slant`）。

---

## 3. 测试案例矩阵与综合复核结果

本次共复核 22 个测试案例（20 个标准测试案例 + 2 个保留标量反例），涵盖静态姿态、极值转向、倾斜、复合姿态、连续时间线、外力物理以及动作隔离。

### 3.1 总体结果概览

- **标准案例通过率**：**20 / 20 PASS** (100%)
- **标量反例复现率**：**2 / 2 成功复现**（几何与像素均 PASS，标量精准复现超差）
- **最大几何误差**：$7.197556\times 10^{-5}\text{px}$（远低于 $0.001\text{px}$ 门槛）
- **图像像素超差**：所有 22 个案例超差像素数**全部为 0**
- **拓扑匹配**：所有案例图元、顶点、三角形与 WebGL 参照 100% 对齐

### 3.2 详细案例数据表

| 案例名称 | 模型 | 类别 | 几何最大误差 (px) | 标量最大误差 | 像素超差数 | 判定 |
|---|---|---|---|---|---|---|
| `fraise-france-maid-b_eye0_default` | Fraise France | 静态 | 0.0000e+00 | 0.0000e+00 | 0 | **PASS** |
| `fraise-france-maid-b_eye10_neutral` | Fraise France | 静态 | 0.0000e+00 | 0.0000e+00 | 0 | **PASS** |
| `fraise-france-maid-b_eye10_head_lr30` | Fraise France | 静态 | 3.4120e-05 | 0.0000e+00 | 0 | **PASS** |
| `fraise-france-maid-b_eye10_head_lr_minus30` | Fraise France | 静态 | 3.4120e-05 | 0.0000e+00 | 0 | **PASS** |
| `fraise-france-maid-b_eye10_body_lr30` | Fraise France | 静态 | 0.0000e+00 | 0.0000e+00 | 0 | **PASS** |
| `fraise-france-maid-b_eye10_body_lr_minus30` | Fraise France | 静态 | 0.0000e+00 | 0.0000e+00 | 0 | **PASS** |
| `fraise-france-maid-b_body_slant30` | Fraise France | 静态 | 3.0518e-05 | 0.0000e+00 | 0 | **PASS** |
| `fraise-france-maid-b_head_slant30` | Fraise France | 静态 | 3.0518e-05 | 0.0000e+00 | 0 | **PASS** |
| `fraise-france-maid-b_combined_pose` | Fraise France | 静态 | 6.5631e-05 | 0.0000e+00 | 0 | **PASS** |
| `fraise-france-maid-b_source-physics60` | Fraise France | 回放 | 4.8253e-05 | 0.0000e+00 | 0 | **PASS** |
| `chocola-date-a_full-idle24` | Chocola Date | 回放 | 1.7994e-05 | 0.0000e+00 | 0 | **PASS** |
| `chocola-date-a_full-jump24` | Chocola Date | 回放 | 1.7060e-05 | 0.0000e+00 | 0 | **PASS** |
| `fraise-maid-a_base` | Fraise Maid A | 静态 | 0.0000e+00 | 0.0000e+00 | 0 | **PASS** |
| `fraise-maid-a_head_lr30` | Fraise Maid A | 静态 | 7.1976e-05 | 0.0000e+00 | 0 | **PASS** |
| `fraise-france-maid-b_direction-diff` | Fraise France | 回放 | 3.4120e-05 | 0.0000e+00 | 0 | **PASS** |
| `fraise-france-maid-b_direction-add` | Fraise France | 回放 | 1.5259e-05 | 0.0000e+00 | 0 | **PASS** |
| `fraise-france-maid-b_direction-main` | Fraise France | 回放 | 2.2888e-05 | 0.0000e+00 | 0 | **PASS** |
| `fraise-france-maid-b_source-timeline60` | Fraise France | 回放 | 3.1457e-05 | 0.0000e+00 | 0 | **PASS** |
| `fraise-france-maid-b_force-positive30` | Fraise France | 回放 | 3.0518e-05 | 8.5831e-06 | 0 | **PASS** |
| `fraise-france-maid-b_force-half-negative30` | Fraise France | 回放 | 1.5259e-05 | 2.8610e-06 | 0 | **PASS** |
| `fraise-france-maid-b_force-negative30` | Fraise France | 回放 | 3.0518e-05 | **1.6689e-05** | 0 | **COUNTEREXAMPLE** |
| `fraise-france-maid-b_force-half-positive30` | Fraise France | 回放 | 1.5259e-05 | **1.1444e-05** | 0 | **COUNTEREXAMPLE** |

---

## 4. 关键特性独立视觉与形态分析

针对 workboard 明确要求的局部区域，Gemini 进行了独立多模态视像审查，重点检查高对比度边界、半透明过渡区和多层裁剪边界。

### 4.1 Fraise France Maid B: 默认眼态与可选遮罩解析
- **背景**：V7 阶段默认眼态（`face_eye_open = 0`）会因找不到 `add_mask` 而报错误码 8 (`UnsupportedMask`) 导致渲染终止。
- **现实验证**：
  - `geometry_scene.cpp:590-605` 正确将全模型未声明的 `add_mask` 作为缺省可选模板忽略；
  - 局部裁图 `eye_stencil` 显示：眼白（`shirome`）主遮罩完全正常建立；
  - 绿色渐变虹膜、猫眼竖瞳、主高光以及下部微小高光（`tex/0096`）均严密贴合眼睑轮廓，无任何形状裁切错位或边界溢出；
  - 16x 放大残差图在眼部区域呈现纯黑（0 像素差异）。

### 4.2 Fraise France Maid B: 自动镜像参数取反
- **测试输入**：直接传作者参数 `head_LR = 30`、`head_LR = -30`、`body_LR = 30`、`body_LR = -30`、`head_slant = 30`、`body_slant = 30`。
- **视觉特征核对**：
  - `head_LR = 30` 时，头部向画面左侧偏转（对应模型视角的右转）；
  - `head_slant = 30` 时，头部整体向画面右侧倾斜（对应模型视角的左倾）；
  - `combined_pose`（`head_LR=30, head_UD=30, body_LR=-15, face_eye_open=0`）下，头部仰角与躯干反向侧拧复合变形，面部透视缩短准确，衣领与胸口蝴蝶结保序渲染；
  - 面部轮廓（`contour`）、猫耳外沿尖端及内部白色绒毛（`ear_edges`）完全贴合 WebGL 参考画面，几何误差控制在 $6.56\times 10^{-5}\text{px}$ 之内，像素超差点为 0。

### 4.3 Chocola Date A: 动作裁剪组独立性（Motion Clip Isolation）
- **核心逻辑**：`geometry_scene.cpp:632-640` 按 `(mask_id, scope)` 对裁剪组归类。
- **视觉特征核对**：
  - 在 `full-idle24`（待机动作）中，瞳孔与独立高光在同一眼眶模板下各自裁剪，高光边缘整洁，无残缺；
  - 在 `full-jump24`（跳跃动作）中，角色躯干大幅度垂直位移，眼部与发丝处于动态运动状态，动作特有的图层未与静态图元发生遮罩污染；
  - 两组回放的几何误差分别为 $1.799\times 10^{-5}\text{px}$ 和 $1.706\times 10^{-5}\text{px}$，像素超差均为 0。

### 4.4 Fraise Maid A: 真实存在 add_mask 的主动参与
- **检查要点**：确认修复可选遮罩时未通过简单字符串匹配把真实的 `add_mask` 剔除。
- **代码与运行时证据**：
  - `fraise-maid-a` 的 `model.nkm.json` 中，`目L` 和 `目R` 各自拥有 `children[1]` 为 `add_mask`，其属于局部 scope；
  - `geometry_scene.cpp:585-588` 匹配 `scope == node.scope && source_label == name`，成功将其作为遮罩 root 加入 `roots` 列表；
  - 绘制诊断显示该遮罩被正确写入 FBO 2 并在复合眼球中生效；
  - `fraise-maid-a_base` 与 `fraise-maid-a_head_lr30` 的图像像素差均为 0，几何误差最大 $7.198\times 10^{-5}\text{px}$。

---

## 5. 保留反例分析（Documented Counterexamples）

在 C5 遗留的受控外力物理测试中，有两例在满足几何（$\le 3.05\times 10^{-5}\text{px}$）与图像（0 像素差）的前提下，标量变量超出 $1.0\times 10^{-5}$ 公差门槛：

### 5.1 反例 1：`force-negative30`
- **输入条件**：`origin = [50, 350]`, `scale = 0.8`, `variables = {"head_LR": 20, "head_slant": 10, "move_LR": 15}`, `displacement = {"bust": [-3, 1], "hair": [-3, 1], "parts": [-3, 1]}`, 30 步。
- **实测表现**：
  - 几何最大误差：$3.051758\times 10^{-5}\text{px}$（PASS）
  - 图像超差像素数：0（PASS）
  - 标量最大误差：$1.668930\times 10^{-5}$（**FAIL**）
  - 超差变量与步数：`hair_UD_front`，第 3 步（作者值与原生值在第 6 位小数产生微小散度）。

### 5.2 反例 2：`force-half-positive30`
- **输入条件**：`origin = [50, 350]`, `scale = 0.5`, `variables = {"head_LR": 20, "head_slant": 10, "move_LR": 15}`, `displacement = {"bust": [3, -1], "hair": [3, -1], "parts": [3, -1]}`, 30 步。
- **实测表现**：
  - 几何最大误差：$1.525879\times 10^{-5}\text{px}$（PASS）
  - 图像超差像素数：0（PASS）
  - 标量最大误差：$1.144409\times 10^{-5}$（**FAIL**）
  - 超差变量与步数：`hair_UD_front`，第 26 步。

### 5.3 结论与移交
- 两个案例的画面与网格均已达到发布态精度，但物理阻尼积分在非单位缩放和非零 origin 复合外力下仍有 $10^{-5}$ 量级的浮点误差累积。
- 本次复核严格遵循项目规范，**不放宽公差门槛，不伪造 PASS**，完整保留输入、日志、图象及 hash，移交 Codex 后续推导。

---

## 6. 证据产物清单

全部测试产物已落盘至 `build/evidence/v8/`：

```text
build/evidence/v8/
├── v8_ledger.json                                           # 完整机器可读账本（含哈希、判决与阈值）
├── <case>_native.png                                        # 原生渲染输出
├── <case>_geo.json                                          # 几何与拓扑差异报告
├── <case>_compare.json                                      # 黑白底与 alpha 像素公差报告
├── <case>_compare.png                                       # 全画幅 3-Panel 对照图 (Ref | Nat | 16x Diff)
└── crops/
    ├── <case>_crop_<region>_ref.png                         # WebGL 参考局部裁图
    ├── <case>_crop_<region>_nat.png                         # 原生局部裁图
    ├── <case>_crop_<region>_diff16x.png                     # 16x 放大残差裁图
    └── <case>_crop_<region>_sbs.png                         # 局部 3-Panel 复合对照图
```

---

## 7. 验收结论与后续交接

1. **Gemini 独立验收结论**：
   - schema 6 镜像参数展开公式、可选遮罩容错与 motion clip group 隔离在所测代表案例上**完全成立**。
   - 所有 20 个标准案例在严格门槛（几何 $\le 0.001\text{px}$、拓扑全匹配、标量 $\le 10^{-5}$、超差像素为 0）下**全部通过**。
   - 2 个物理标量反例已精确复现并锁定在 $1.1\times 10^{-5}\sim 1.7\times 10^{-5}$，图像与几何不受影响。
2. **移交 Codex**：
   - 接续处理 C5 剩余 15 项 CPU 目录失败、0x7fc 精确处理与嵌套遮罩；
   - 推进 C6 应用生命周期与 C7 Qt Quick 宿主。
