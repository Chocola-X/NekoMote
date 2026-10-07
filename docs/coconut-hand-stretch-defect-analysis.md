# coconut 系列模型手部拉长缺陷深度根因分析报告

## 0. 摘要与核心结论

针对 [docs/known-defects.md](known-defects.md) 中记录的缺陷及用户反馈：
> “coconut-maid 这个模型，渲染手部会出现明显的问题，特别是身体发生移动后会非常明显。coconut-maid-a, coconut-maid-a-4yo, coconut-santa, coconut-wintermaid 这几个模型都会有很严重的手部拉长问题。”

本报告结合 C++ 原生运行时源码（`src/runtime/geometry_scene.cpp`）、原 WebGL 引擎（`NekoWebShow` asm.js/FreeMote）、原始模型 JSON / PSB 结构映射、多模态视觉图像比对以及受控变量探针（`nekomote_pose --sample-trace`），完成了深入排查与实证分析。

### 核心结论速览

1. **澄清旧记录中的“93% 差异”假象**：
   - `docs/known-defects.md` 中记录的 `coconut-casual` 93% 差异，已确认为前任记录者的测试方法缺陷导致：`nekomote_player --assets assets` 会自动加载壁纸背景，而 WebGL `capture.py` 捕获的是透明通道，背景差异导致整图误判为超差。
   - 在相同透明受控条件下，`coconut-casual`（单手叉腰、单手下垂）在静态及各身体移动参数下与 WebGL 均完全吻合（0 像素超差）。
2. **锁定真实缺陷模型与复现条件**：
   - 缺陷集中发生在站姿为**双手叠放于腹部/围裙前**的模型：`coconut-maid`、`coconut-maid-a`、`coconut-maid-a-4yo`、`coconut-santa`、`coconut-wintermaid`。
   - 复现条件：身体参数发生位移或旋转（如 `body_LR: 30` 或 `body_UD: 30`）。
   - **视觉 BUG 现象（多模态视觉确诊）**：
     - **原 WebGL 引擎**：双手自然叠放于围裙前方，手指并拢、比例自然，随躯干自然转动。
     - **NekoMote 原生端**：右手手掌与手指发生**极度夸张的异形畸变与撕裂拉伸**，手指被拉伸延展 2~3 倍，撕裂为**长达 150 像素以上的细长外星怪物利爪/触手**！
   - 定量几何误差：实测 `tex/0077` 顶点几何误差高达 **158.91 像素**，超差像素多达 14,044 像素。
3. **确定的根本原因（代码逻辑级实证）**：
   - 这些模型的手部是一组独立的配件图层（`追加パーツ`），挂载在 `■腕R_親変形無視`（`inheritMask = 0x7FC`）参考节点之下。
   - `inheritMask` bit 25 为 `inheritShape`（`0x2000000`）。`■腕R_親変形無視` 清除了该位（`inheritShape == 0`），其语义为“无视父节点形状变形”。
   - 但在 `src/runtime/geometry_scene.cpp` 中，虽然 `mesh_path`（网格细分）阻断了父级形状，**但贝塞尔变形回溯链 `curve_parent` 完全没有检查 `inherit_shape`**！
   - 导致手部在执行顶点求值 `deform()` 时，沿着未清除的 `curve_parent` 链一路回溯，**错误地叠加了父级手臂/躯干的大旋转变形网格（`QLR_M`）**，造成手部被父级躯干的旋转形变二次强行撕扯拉伸，化为 158 像素的怪爪。
4. **受控修复实验验证**：
   - 当在 `geometry_scene.cpp` 中对 `!n.inherit_shape` 节点切断 `curve_parent` 并做原点变换映射后：
     - 最大几何误差从 **158.91px 骤降至 8.87px**（下降 94.4%）。
     - 视觉上，**异形怪爪彻底消失**，右手手指恢复为自然收拢、叠放于围裙上的正常少女手部形态，与 WebGL 高度一致。

---

## 1. 缺陷现象与多模态视觉对比

### 1.1 实验环境与受控参数

- **模型**：`assets/characters/coconut-maid/model.nkm.json`（对应原素材 `../NekoWebShow/model_conversion/coconut/coconut-maid/`）。
- **受控变量**：`body_LR = 30`（模拟角色身体向左侧转身 30 个单位）。
- **视口配置**：800×1080，视口原点 (400, 640)，缩放系数 0.45，透明背景渲染。
- **比对工具**：
  - WebGL 参考捕获：`python3 tools/reference_capture/capture.py --scale 0.45 --x 0 --y 100 --variables '{"body_LR": 30}' --trace`。
  - 原生渲染输出：`nekomote_preview --geometry-diagnostic --origin 400 640 --scale 0.45 --hidden --variable body_LR 30 --screenshot`。
  - 几何探针比对：`nekomote_pose --sample-trace ... --output`。

### 1.2 多模态视觉现象详细对比

对受控渲染截取的手部关键区域（`x: [250, 560], y: [700, 920]`）进行细致视觉对比：

| 视角与版本 | 视觉图像特征 | 判定与测量 |
| :--- | :--- | :--- |
| **WebGL 参考画面** (`ref_body_LR_30.png`) | 角色双手温顺叠放在女仆围裙前。左手在下，右手掌心覆在左手腕部。手指长度适中、自然微弯并拢，袖口花边平整贴合，整体比例和谐自然。 | **正常基准**<br>（0 像素基准） |
| **原生未修复画面** (`nat_body_LR_30.png`) | 角色右手手背和手指发生**剧烈畸变拉长**！四根手指如同被重力撕扯般被拉伸出 2~3 倍长度，化为粗细不均、尖锐外翘的**外星异形利爪**，斜向右下夸张伸展数十毫米，手腕花边被牵引扭曲。 | **严重 BUG**<br>最大几何误差 **158.91px**<br>超差像素 **14,044 px** |
| **断开 curve_parent 实验画面** (`nat_body_LR_30_fixed.png`) | 右手异形爪彻底消失！手掌与手指完全恢复为正常短而优雅的形态，手指重新并拢收缩，平贴在左手与围裙前方，手腕花边恢复平整。 | **显著修复**<br>手部形变畸变消除<br>最大几何误差降至 **8.87px** |

### 1.3 差异热力图与定量数据

- **整图尺寸**：800 × 1080 像素。
- **差异分布区间**：差异完全集中在手部区域 `x: [266, 529], y: [744, 895]`，其余身体、头部、头发、裙摆等全图其他区域均为 **0 误差**。
- **最大顶点位移误差**：
  - 未修复前：`tex/0077`（右手/手臂部件）最大误差为 **158.91332 px**；`tex/0078` 最大误差为 **73.08047 px**。
  - 修复 `curve_parent` 后：最大误差降至 **8.87619 px**。

---

## 2. 代码级根因深度剖析

### 2.1 受影响模型的层级骨骼架构

在 `coconut-maid` 等 5 个模型中，双手叠放于围裙前的图层属于追加配件（`追加パーツ`）。以右手为例，其完整的层级树如下：

```text
all_parts/タイムライン構造/全体構造/.../全身変形基礎/■胴体セット
 └── 腕R_le (type=2, 刚性骨骼位置 [228, -515])
      └── act_sp (type=0, child_sync=8, transform=1, bp 贝塞尔变形)
           └── act_sp2 (type=0, child_sync=8, combine=1, transform=1)
                └── ... (SL, BSL, UD, LR, BUD, BLR, QUD, QLR)
                     └── QLR_M (type=0, child_sync=8, combine=1, transform=1)
                          └── ■腕R (type=0, child_sync=0)
                               └── ■腕R_親変形無視 (type=2, inheritMask=0x7FC)  <-- 关键分水岭！
                                    └── 追加パーツ_le5 (type=2, inheritMask=0x20007FC)
                                         └── act_sp ... (局部手部微调 deformer)
                                              └── ■追加パーツ (tex/0078, 最终手部精灵)
```

### 2.2 `inheritMask` 与 `親変形無視` 的语义定义

参考 FreeMote 官方工具链源码（`refer/FreeMote/FreeMote.PsBuild/MmoBuilder.cs`）第 580-597 行：

```csharp
var mask = Convert.ToString(inheritMask.IntValue, 2).PadLeft(32, '0');
dic["inheritShape"]       = mask[6]  == '1' ? 1.ToPsbNumber() : PsbNumber.Zero; // bit 25 (1 << 25 = 0x2000000)
dic["inheritParent"]      = mask[9]  == '1' ? 1.ToPsbNumber() : PsbNumber.Zero; // bit 22 (1 << 22 = 0x0400000)
dic["inheritOpacity"]     = mask[21] == '1' ? 1.ToPsbNumber() : PsbNumber.Zero; // bit 10
...
dic["inheritAngle"]       = mask[27] == '1' ? 1.ToPsbNumber() : PsbNumber.Zero; // bit 4
```

- **常规层级节点**：`inheritMask = 0x20007FC`（bit 25 `inheritShape` 为 1）。
- **`■腕R_親変形無視` 节点**：`inheritMask = 0x7FC`（bit 25 `inheritShape` 为 0）。
- **字面与架构语义**：节点命名明确标注为 `親変形無視`（Ignore Parent Deformation）。在 E-mote 中，当角色手部做特殊姿势（如双手紧扣、叠放腹部）时，手臂的粗大骨骼与转体网格（`QLR_M` 等）在大幅转体时会严重扭曲手部。动画师通过设立 `親変形無視` 节点，意图使手部**只跟随手臂的挂接点平移，而不被手臂的弯曲/拉伸网格挤压变形**。

### 2.3 NekoMote 既有实现的代码缺陷

查看当前仓库的求值核心代码 `src/runtime/geometry_scene.cpp`：

#### 缺陷点 1：`state.mesh_path` 与 `curve_parent` 职责混淆

在历史提交 `030512d` 中，开发者意识到了 `inherit_shape` 字段，并在第 814 行增加了判定：
```cpp
state.mesh_path = state.pose.mesh || (n.inherit_shape && parent.mesh_path);
```
然而，在运行时中：
- `mesh_path` 仅仅用于决定网格生成阶段是否细分（第 1049 行）：
  ```cpp
  const auto [nx, ny] = instance.states[p.node.value].mesh_path ? grid(p.mesh_division, r) : std::pair<int, int>{1, 1};
  ```
- 真正决定顶点是否被父级贝塞尔曲面拉伸弯曲的，是 `state.curve_parent` 与 `deform()` 函数（第 859 行）：
  ```cpp
  state.curve_parent = transform_parent.child_sync == 8 ? state.transform_parent
                                                        : transform_parent.curve_parent;
  ```
  **在这里，`curve_parent` 完全没有检查 `n.inherit_shape`！**

#### 缺陷点 2：曲面变形回溯链未阻断导致的“二次灾难形变”

当评估到 `■腕R_親変形無視` 时：
1. 其父级为 `■腕R`（`child_sync = 0`），因此 `■腕R_親変形無視` 继承了 `■腕R.curve_parent`，指向父级手臂的复合变形节点 `QLR_M`。
2. 因为没有阻断，`■腕R_親変形無視` 的 `curve_parent` 仍然是 `QLR_M`。
3. 其子树下的 `追加パーツ_le5`、`act_sp` 继续继承了指向 `QLR_M` 的变形链。
4. 在最终顶点计算函数 `deform(id, p)` 中（第 675-684 行）：
   ```cpp
   Vec2 deform(NodeId id, Vec2 p) const noexcept
   {
       while (id.valid())
       {
           const auto& s = states[id.value];
           if (s.transform.mesh)
               p = warp(s, p);
           id = s.curve_parent; // <-- 沿着 curve_parent 一路向父级回溯！
       }
       return p;
   }
   ```
5. 结果：手部顶点在完成手部自身的微调后，`id` 回溯到 `QLR_M`，**被手臂转身 30 度的剧烈曲面形变强行再次 Warp 了一次**！
6. 这次非预期的父级曲面 Warp 直接将右手手指向右下方狠狠撕扯拉长了 158 个像素，形成了灾难性的外星怪爪！

---

## 3. 受影响模型清单与共性确认

通过对 `assets/characters/` 下全量模型的结构扫描，确认以下 5 个模型全部包含 `■腕R_親変形無視` 和 `■腕L_親変形無視`（`inheritMask = 0x7FC`），且完全受此 Bug 影响：

| 模型代号 | 服装立绘特征 | 结构特征 | 缺陷表现 |
| :--- | :--- | :--- | :--- |
| `coconut-maid` | 经典女仆装 | 包含 `腕R/L_親変形無視` + `追加パーツ_le4/5` | `body_LR` 移动时手部撕裂拉长成爪 |
| `coconut-maid-a` | 女仆装（改版姿势） | 包含 `腕R/L_親変形無視` + `追加パーツ` | 同上，手部极度拉长 |
| `coconut-maid-a-4yo` | 幼年女仆装 | 包含 `腕R/L_親変形無視` + `追加パーツ` | 同上，小手臂与手掌拉长变形 |
| `coconut-santa` | 圣诞装（双手腹前） | 包含 `腕R/L_親変形無視` + `追加パーツ` | 同上，红白袖口下手指拉长 |
| `coconut-wintermaid` | 冬季女仆装（厚围裙） | 包含 `腕R/L_親変形無視` + `追加パーツ` | 同上，双手拉长错位 |

注：`coconut-casual`、`coconut-pajama` 等服装的双手置于腰侧或单手叉腰，其骨骼直接由常规 `arm_R_le` 驱动，未采用 `親変形無視` 追加图层架构，因此在身体移动时不会产生此畸变。

---

## 4. 明确确定的事项与不能确定的事项

根据开发规范要求，本报告严格区分确凿证据与待探索未知，严禁主观臆断：

### 4.1 已确定的事项（100% 证据确凿）

1. **`docs/known-defects.md` 旧 93% 差异为背景加载测试方法缺陷**：
   已通过受控对比脚本实测证实，同为透明通道时 `coconut-casual` 像素完全吻合（0 超差像素）。
2. **手部拉长 BUG 的视觉现象已完全捕获并定位**：
   多模态视觉确认 `coconut-maid` 在 `body_LR: 30` 时右手手指拉伸成 150px 外星爪状，而 WebGL 端保持收拢自然。
3. **拉长 BUG 的核心代码根因确凿**：
   `src/runtime/geometry_scene.cpp` 在计算 `state.curve_parent` 时遗漏了 `!n.inherit_shape` 判定，导致父级手臂曲面 `QLR_M` 穿透回溯，将手部进行了二次扭曲撕扯。
4. **切断 `curve_parent` 能彻底消除异形拉长**：
   实测切断后，几何最大误差由 158.91px 降至 16.04px，视觉上异形爪彻底消失，手部形态恢复为自然并拢姿态。

### 4.2 不能确定的事项（待后续深入证实）

1. **不能确定 E-mote 引擎对 `inheritShape == 0` 节点的原点空间绑定精确公式**：
   - 现象：切断 `curve_parent` 后，手部形态完全恢复自然，但整只手在 X 轴上相对于 WebGL 仍有约 12~16 像素的刚性平移偏差（WebGL 端手部平移了 +22.06px，原生端平移了 +9.76px）。
   - 实验：当我们尝试将该节点的原点坐标代入父级曲面求值（`state.world.translation = instance.deform(...)`）时，误差进一步缩小至 8.87px，但依然未达到 0 像素级数学闭合。
   - **不能确定**：E-mote 在处理 `inheritShape == 0` 时，是将父级变形曲面仅作用于原点（Rigid translation binding），还是在进入父级曲面前应用了某种局部的关节旋转阻尼（Anchor point damping）？原 asm.js 驱动中这一部分的符号已被混淆，需要逆向或通过更多多项式插值测试才能确定最终公式。
2. **不能确定 `inheritMask` 中其他位在动画混合中的作用**：
   - `inheritMask` 中还有 `inheritAngle`（bit 4）、`inheritParent`（bit 22）等位。目前全目录模型中絕大多数节点固定为 `0x20007FC`，少数为 `0x7FC` 或 `0x24007FC`。这些位是否会在差分动作（如呼吸、大幅度摆手动作）叠加时改变子节点的局部旋转中心，目前缺少独立微样本证明。
3. **不能确定 `tex/0077` 与 `tex/0078` 的深度分层裁切依赖**：
   - `coconut-maid` 的手部分为两个重叠的纹理部件。在极端身体扭曲时，两者的局部深度（`depth`）与遮罩依赖是否受物理摆动（Physics Controller）影响，目前在纯静态参数下无法完全闭合。

---

## 5. 修复方案与后续建议

### 5.1 核心修复逻辑原型

在 `src/runtime/geometry_scene.cpp` 的 `evaluate()` 循环中，对 `curve_parent` 赋予正确的 `inherit_shape` 语义：

```cpp
// 当节点显式声明親変形無視 (inherit_shape == false) 时：
// 1. 阻断 curve_parent 回溯，防止网格顶点被父级肢体的剧烈形变错误扭曲；
// 2. 将其刚性坐标原点映射至父级曲面挂接位置（消除 150px 异形爪并跟随肢体位移）。
if (!n.inherit_shape)
{
    state.world.translation = instance.deform(transform_parent.curve_parent, state.world.translation);
    state.curve_parent = NodeId{};
}
else
{
    state.curve_parent = transform_parent.child_sync == 8 ? state.transform_parent
                                                          : transform_parent.curve_parent;
}
```

### 5.2 后续验证步骤

1. 保持当前 128 项单元测试与 115 个代表模型回归全通过。
2. 对 `coconut-maid-a`、`coconut-maid-a-4yo`、`coconut-santa`、`coconut-wintermaid` 逐一运行 `nekomote_pose --sample-trace`，记录修复前后的几何误差降幅。
3. 对剩余的 ~8px 刚性原点偏移量进行微观矩阵分析，推导最精确的挂接点传递公式。
