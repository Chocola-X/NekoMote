# V3 源 GPU 混合模式与属性观测证据报告

- 记录日期：2026-10-07
- 执行者：Antigravity
- 模型：Gemini 3.8 Flash (High)
- 上下文：`docs/agent-workboard.md` V3
- 参照源：`../NekoWebShow/`（WebGL 参考播放器）

---

## 1. 任务目标与全目录字段筛选

依据规范，当前原生运行时正式路径严格拒绝非零 `bm` 与非 0/1 的 `attr`。本任务旨在：
1. 遍历素材目录，锁定包含非标准混合 `bm` 与属性 `attr` 的最小真实模型；
2. 在原 WebGL 驱动下观测真实 Draw Call 的 `blendFuncSeparate`、`blendEquationSeparate`、`colorMask`、着色器及 FBO 状态；
3. 从数值、图元与数学层面给出精确的混合语义推导，并交付可独立复跑的证据包与图像。

### 1.1 全目录（195个产品模型）检索结果

对 `assets/characters/*/model.nkm.json` 及原仓库 `model_conversion/` 全部解包 JSON 进行穷举扫描：

| 字段与取值 | 出现次数 | 涉及模型 | 典型节点路径 |
|---|---|---|---|
| `bm = 19` | 5 | `fraise-maid-a`<br>`fraise-maid-b`<br>`milk-summer`<br>`milk-winter` | `.object.head_parts.motion.頭部変形基礎.layer[0].children[6]...■追加パーツ#0` |
| `bm = 16` | 0 | 无（全目录 0 处） | 原理论枚举位，实际模型数据中未出现 |
| `attr = 2` | 1 | `fraise-france-maid-b` | `.source.tex.icon["0029"]`（引用层：`■胸(予備)`） |

**结构排查确认**：
`fraise-maid-a` 与 `fraise-france-maid-b` 均**不包含** `0x7fc`（`inheritShape`），无局部手部继承异常，是受控隔离混合与属性语义的理想最小样本。

---

## 2. WebGL 真实 Draw Call 观测（`tools/reference_capture`）

通过注入增强型 `trace.js`（新增捕获 `blendEquationSeparate`），在固定步进（steps=0）、固定尺寸（800×1080）、禁用随机时钟与物理的无头浏览器环境中捕获真实 WebGL 绘制流。

### 2.1 案例一：`bm = 19`（模型：`fraise-maid-a`）

- **源节点定位**：
  - 路径：`head_parts/頭部変形基礎/追加パーツ_le43/.../■追加パーツ#0`
  - 图元：`icon: "0105"`, `src: "tex"`, 纹理切片 `left: 1758.0, top: 1395.0, width: 298.0, height: 124.0`
  - 角色部件含义：草莓女仆头饰（发箍发带及下方蝴蝶结在头发上的投射阴影）
- **定位到的 WebGL Draw Call**：**Draw #33**
  - **UV 范围**：`min(u,v) = (1758, 529)`, `max(u,v) = (2056, 653)`（纹理尺寸 4096×2048，Y轴翻转精确匹配：`2048 - (1395 + 124) = 529`）
  - **屏幕包围盒**：`[x: 251..549, y: 270..394]`
  - **FBO 绑定**：`true`（离屏颜色缓冲区）
  - **colorMask**：`[GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE]`
  - **blendFuncSeparate**：
    - `srcRGB = GL_DST_COLOR`（774）
    - `dstRGB = GL_ONE_MINUS_SRC_ALPHA`（771）
    - `srcAlpha = GL_ZERO`（0）
    - `dstAlpha = GL_ONE`（1）
  - **blendEquationSeparate**：
    - `rgbEquation = GL_FUNC_ADD`（32774 / 0x8006）
    - `alphaEquation = GL_FUNC_ADD`（32774 / 0x8006）
  - **Uniforms**：
    - `u_texSize = [4096, 2048]`
    - `u_texUnitId = 0`
  - **片元着色器（Fragment Shader）源码**：
    ```glsl
    precision mediump float;
    varying highp vec2 v_texCoord;
    uniform sampler2D u_texUnitId;
    void main()
    {
        vec4 tmp = texture2D(u_texUnitId, v_texCoord);
        tmp.rgb *= tmp.a;
        gl_FragColor = tmp;
    }
    ```

#### 数学推导与语义闭环：
片元着色器先将源纹理 RGB 预乘透明度：
$$C_{\text{frag}} = C_{\text{src}} \cdot A_{\text{src}}, \quad A_{\text{frag}} = A_{\text{src}}$$

送入混合方程：
$$C_{\text{result}} = C_{\text{frag}} \cdot \text{GL\_DST\_COLOR} + C_{\text{dst}} \cdot \text{GL\_ONE\_MINUS\_SRC\_ALPHA}$$
$$C_{\text{result}} = (C_{\text{src}} \cdot A_{\text{src}}) \cdot C_{\text{dst}} + C_{\text{dst}} \cdot (1 - A_{\text{src}})$$
提取公因式 $C_{\text{dst}}$：
$$C_{\text{result}} = C_{\text{dst}} \cdot \big[ 1 - A_{\text{src}} + C_{\text{src}} \cdot A_{\text{src}} \big]$$
$$C_{\text{result}} = C_{\text{dst}} \cdot \text{lerp}(1.0, \, C_{\text{src}}, \, A_{\text{src}})$$

Alpha 通道方程：
$$A_{\text{result}} = A_{\text{frag}} \cdot 0 + A_{\text{dst}} \cdot 1 = A_{\text{dst}}$$

**推导结论**：
1. **RGB 语义**：当 $A_{\text{src}} = 1$ 时，$C_{\text{result}} = C_{\text{dst}} \cdot C_{\text{src}}$，为纯正的正片叠底（Multiply）；当 $A_{\text{src}} = 0$ 时，$C_{\text{result}} = C_{\text{dst}}$，背景不受影响；在半透明区域以 $A_{\text{src}}$ 作为权重向 $1.0$ 线性混合。这是在直通 Alpha 图集上进行**正片叠底（Multiply）**的标准精确工业公式。
2. **Alpha 语义**：目标 Alpha 保持原样不被覆盖或稀释（$A_{\text{result}} = A_{\text{dst}}$）。
3. **E-mote 枚举确认**：**`bm = 19` 对应正片叠底（Multiply）**。

---

### 2.2 案例二：`attr = 2`（模型：`fraise-france-maid-b`）

- **源节点定位**：
  - 路径：`.source.tex.icon["0029"]`
  - 图元：`attr: 2`, `left: 4080.0, top: 184.0, width: 11.0, height: 11.0`
  - 角色部件含义：胸前围裙装饰点（`■胸(予備)`）
- **定位到的 WebGL Draw Call**：**Draw #9**
  - **UV 范围**：`min(u,v) = (4080, 3901)`, `max(u,v) = (4091, 3912)`（图集 4096×4096，`4096 - (184+11) = 3901`）
  - **屏幕包围盒**：`[x: 376..387, y: 937..948]`
  - **FBO 绑定**：`true`
  - **colorMask**：`[GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE]`
  - **blendFuncSeparate**：
    - `srcRGB = GL_SRC_ALPHA`（770）
    - `dstRGB = GL_ONE_MINUS_SRC_ALPHA`（771）
    - `srcAlpha = GL_ONE`（1）
    - `dstAlpha = GL_ONE_MINUS_SRC_ALPHA`（771）
  - **blendEquationSeparate**：`(GL_FUNC_ADD, GL_FUNC_ADD)`
  - **片元着色器（Fragment Shader）源码**：标准直通纹理采样（无预乘，`gl_FragColor = texture2D(u_texUnitId, v_texCoord);`）

#### 机制对比：
- 在 `fraise-france-maid-b` 中，带有 `attr = 1` 的图层（如 Draw #0、#3、#6 等），WebGL 均分两遍绘制：第一遍采用 `blend = (773, 772, 0, 1)`（`GL_ONE_MINUS_DST_ALPHA, GL_DST_ALPHA, GL_ZERO, GL_ONE`）进行边缘透明度保护，第二遍才绘制本体；
- 而对于 `attr = 2` 的 Draw #9，WebGL **仅执行单遍绘制**，无任何附加保护 Pass。

**推导结论**：
**`attr = 2` 对应单遍标准覆盖（ColorProtection::None）**。它表示该 Sprite 不需要执行双遍透明保护。

---

## 3. 证据文件指纹与哈希审计

### 3.1 资产与参照文件 SHA-256 账本

| 资产类型 | 资产路径 | SHA-256 哈希 |
|---|---|---|
| `fraise-maid-a` 源 PSB Zip | `../NekoWebShow/data/fraise-maid-a.pure.psb.zip` | `cea1488619a235d95add88b50ffc8424d4bd1afa7357cd470d69171405d83788` |
| `fraise-maid-a` 内部 PSB | `fraise-maid-a.pure.psb` | `2868780aafdfe76f4b85cbf47b099f6918c1f9bcb98bd92745c8dcfd21c64bad` |
| `fraise-maid-a` 解包 JSON | `../NekoWebShow/model_conversion/fraise/fraise-maid-a/fraise-maid-a.json` | `ea91962644e58d544da9ca246f211dbb69b38a93a0256fb9d60baa21f0731a62` |
| `fraise-maid-a` 导入资产 JSON | `assets/characters/fraise-maid-a/model.nkm.json` | `299f021584d7b5de2c80f57b8012e9d1e90d187385a2e131e5c1f5a8f566e33c` |
| `fraise-maid-a` WebGL 参考图 | `build/evidence/v3/fraise-maid-a.png` | `6a0d14ce503f4254bbc2573fbdca033856611edf3bd4c3e60441f3e471c82fe5` |
| `fraise-maid-a` WebGL Trace | `build/evidence/v3/fraise-maid-a.trace.json` | `36883c49241bde3b93af2aee7ac7eb068ed442fbd1e790aec46065f7a9a10359` |
| `fraise-france-maid-b` 源 PSB Zip | `../NekoWebShow/data/fraise-france-maid-b.pure.psb.zip` | `0cf635650ba9a1d57c8e1f275c8e1ac54944e3a8bfea69220c548d97940e8fe2` |
| `fraise-france-maid-b` 内部 PSB | `fraise-france-maid-b.pure.psb` | `92a03929e9ab9fc86a9f3ef2569cd82e380c02d937b1e105f617e50ede42323d` |
| `fraise-france-maid-b` 解包 JSON | `../NekoWebShow/model_conversion/fraise/fraise-france-maid-b/fraise-france-maid-b.json` | `9e9d0678c9e9994b9f250d10262827c0dd1e65d7eb6a1b5f8174f9b49a1ee42f` |
| `fraise-france-maid-b` 导入资产 JSON | `assets/characters/fraise-france-maid-b/model.nkm.json` | `a5442bea99468cce47466a77bc0869701845826d829c1532b55d77f78e5d1f7a` |
| `fraise-france-maid-b` WebGL 参考图 | `build/evidence/v3/fraise-france-maid-b.png` | `94fe9d644b3fd0e498f11e441258e6422634cf66f58fad86a5e83c712dd3646b` |
| `fraise-france-maid-b` WebGL Trace | `build/evidence/v3/fraise-france-maid-b.trace.json` | `4c4a48a21aeca8e0a249ffd26b64abfb4c1d2b27cb084731515748eec0c86563` |

### 3.2 图像合成与局部高倍裁切包（`build/evidence/v3/`）

- `fraise-maid-a_black.png` / `fraise-maid-a_white.png`：透明直通通道与纯黑/纯白底合成图。
- `crop_fraise-maid-a_bm19_trans.png`：Draw #33 包围盒（发箍阴影与蝴蝶结）透明裁切。
- `crop_fraise-maid-a_bm19_black.png` / `crop_fraise-maid-a_bm19_white.png`：黑白背景正片叠底对比裁切，清晰可见半透明阴影与底层头发色彩的平滑相乘。
- `fraise-france-maid-b_black.png` / `fraise-france-maid-b_white.png`：纯黑/纯白底合成图。
- `crop_fraise-france-maid-b_attr2_trans.png` / `_black.png` / `_white.png`：Draw #9 包围盒（围裙装饰）裁切。

---

## 4. 原生运行时状态（合规确认）

为防止未验证公式过早放行，已对原生播放器进行实机调用校验：
```sh
build/dev/bin/nekomote_player --assets assets --character fraise-maid-a --mute --no-save
# 输出：NekoMote: session failure 6/3 (NEKOMOTE_ERROR_UNSUPPORTED)

build/dev/bin/nekomote_player --assets assets --character fraise-france-maid-b --mute --no-save
# 输出：NekoMote: session failure 6/3 (NEKOMOTE_ERROR_UNSUPPORTED)
```
**原生合规状态**：原生引擎当前严格执行对非零 `bm` 和 `attr != 0 && attr != 1` 的明确拒绝，未通过伪装跳过或无证据近似掩盖缺陷。

---

## 5. 后续交付建议（面向 Codex）

根据本次 WebGL 观测获得的精确数学公式与状态机数据，建议 Codex 在实现原生映射时：
1. **对于 `bm = 19`**：
   - 在 `render_gl` 协议中新增 `BlendMode::Multiply`；
   - 着色器或 GL 混合方程配置为：
     `glBlendFuncSeparate(GL_DST_COLOR, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE)`，着色器输出预乘 `tmp.rgb *= tmp.a`，方程为 `GL_FUNC_ADD`；
   - 目标 FBO Alpha 保持不变。
2. **对于 `attr = 2`**：
   - 在 `geometry_scene.cpp:809` 中，将合法 `attr` 校验放宽至 `0, 1, 2`；
   - 在 `geometry_scene.cpp:1117` 中，映射关系为：
     `r.attr == 1 ? ColorProtection::PreserveTranslucent : ColorProtection::None`（即 `attr = 2` 与 `attr = 0` 一致，均使用 `ColorProtection::None` 单遍绘制）。
3. 待 Codex 依据上述证据在 runtime / render_gl 闭合映射并构建后，通过冻结阈值重新开展端到端像素回归验证。
