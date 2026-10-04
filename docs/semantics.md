# 字段语义与证据

源文件可读及 FreeMote 工具的枚举名称，只能支持候选解释。正式求值需受控参考画面/几何验证；未知语义不会在正式加载时静默近似。

| 字段 | 状态 | 当前证据与下一步 |
| --- | --- | --- |
| source.*.texture.pixel | 结构已验证 | `#resource#N` 对应 resx.Resources[N]；需解码图片核对尺寸 |
| motion.parameter / layer.parameterize | 固定姿态映射已验证 | 线性范围/端点、null 覆盖与组合控制见下文；其他离散化拒绝 |
| frame.content 缺失/null | 固定姿态已验证 | type=0/无 content 不可见；type=2 hold、type=3 linear；type=1 未验证且选中时拒绝 |
| mesh.bp | 固定姿态已验证，精度门槛未闭合 | 4×4 双三次 Bézier、域和组合见 30 组证据；不可推广到 cc/所有继承 |
| mesh.cc | 未验证 | 保留任意数量有限分量，不能假定和 bp 一样为 32 |
| meshCombine / meshSyncChildMask | 未验证 | 空白变形层与图像层嵌套；需单变量与组合输入实验 |
| content.mask | 未验证 | 很多非零值对应内容属性存在位，不应直接当成 alpha 遮罩 ID |
| inheritMask / transformOrder | 未验证 | 保留原值并记录参考实验；不能直接全部继承父矩阵 |
| stencilCompositeMaskLayerList | type=5 单层 union 已验证 | motion 实例内引用、每次清空及裁切见 P4；嵌套/反向/blank 源仍拒绝 |
| metadata.customPartsOrder | 未验证 | 主模型值为字符串数字，不是解析后的 motion 名称；禁止当作当前部件绘制顺序 |
| variableList.frameList | 结构已验证 | 是标记帧列表；不能据此声称获得运行时默认值 |
| timelineControl | 结构已验证，求值未验证 | 轨道/空帧/easing/diff/loop 单独保存；P5 对照 |
| metadata 物理/控制配置 | 未实现 | 完整 source 审计归档保留数据，当前资产不宣称具备物理能力 |

## P3 几何实验（进行中）

当前证据为 `docs/validation/p3-geometry.json`：30 组、4 个型号、物理关闭、固定输入、原网页真实顶点。阈值冻结为 0.001 像素；30 组均没有缺失的候选可见部件，最大误差 <0.0003 像素。**这是离线候选场景验证，P3 的 C++ 场景编译/求值器和网格划分仍待完成；不是完整画面验收。**

1. motion 的 parameterize 提供层级默认参数，layer 非 null 索引覆盖；motion 级 parameter 表在 layer 无表时使用。范围到时间映射为线性 clamp，division 为时间终点。默认变量为 0 的结论来自四个参考模型初态，不来自 marker frameList。离散化和其他初态待单独验证。
2. `src=blank` 的四元字符串为 `width:height:originX:originY`，不是 atlas rectangle。sprite 坐标先减图像原点。4×4 bp 为行优先的归一化双三次 Bézier 控制点；mesh null 为单位形状。刚性父矩阵先累计并放置点；控制点编译到放置后的世界空间，独立域从子到父施加。逆域矩阵、控制点和每域 UV 的 Float32 保存边界见 docs/validation/p6-world-deformation.json。源 angle 以度插值，负角度包装到 [0,360) 保存 Float32，再以 double 转弧度/三角函数并保存 Float32 矩阵系数；opa 为 0..255。`head_LR=-1` 的鼻子源 opa=10，参考顶点 alpha=10。
3. 连续且同域、零自身变换的 meshCombine=1 helper 从当前 helper 向最近父级逐层将相对单位形状的位移累加，每层保存 Float32，之后只求值一次；不能复用父级聚合值，因为加法顺序改变保存位。逐层复合公式不正确。组合实验曾有 3.2 像素偏差；关闭成对变量控制的内存 fixture 将位移叠加公式单独验证到 <0.0003 像素。非零 helper 变换、不同域 combine、meshSyncChildMask 的其他位、部分继承和 cc 尚未确认。
4. 对称范围 [-R,R] 的 clampControl type=0 在网格求值之前处理变量对。先把两分量限制到范围，令 `M=max(abs(x/R),abs(y/R))`、`q=min(abs(x/R),abs(y/R))/M`。输出半径 `r=M+(sin(M*pi/2)-M)*(1-cos(q*pi/2))`，两分量乘 `r/(M*sqrt(1+q*q))`；零向量保持零。方向不变，小输入可能扩大，不能用通常的圆形限幅替代。公式来自位置变量实验和数学调用诊断，最终通过未经修改的原 PSB 几何对照；type=1、非对称范围、重叠控制对仍待验证。
5. 普通可见部件深度为祖先 coord.z 之和，按升序保序。priority 和遮罩产生的排序/命令分组尚未确认。

最小错误复现：`head_LR=15, head_UD=-20, body_LR=-15, body_UD=20, head_slant=10`。直接逐层 mesh 复合最大误差 3.213px；只加控制点而遗漏 clamp 为 1.866px；二者同时正确的候选输出为 0.000285px。参考 PSB hash、顶点 trace hash、PNG hash、规范化资产 hash 都在证据 JSON 中。

原网页 GPU 顶点只能供诊断采样；不得作为原生渲染数据。网格细分不能用“均匀 8 段”代替原版：目前实际可见顶点证据提示 subdivision 与变形域/图片裁切相关。遮罩、颜色、透明边缘和全图像还需 P4 截图对照。

## 原生场景与细分（P3 进行中）

固定姿态 C++ 场景的实际顶点及三角形对照通过 30 组、四个型号；证据见 [原生场景记录](p3-native-scene.md)。非零 meshDivision 的 X/Y 段数为 floor(D*w/(w+h))+1 和 D-floor(D*w/(w+h))+1。priority=2、D=0 的密度取当前有 BP 的父域最大 D/(宽+高)，乘图像宽高和取整；每个最终网格轴至少两段。null BP 不启用细分；显式单位 BP 启用。

初始“按图像尺寸固定 3/64 密度”的候选在 body_LR=30 与 azuki 初态失败。进一步用 213×213 fixture 区分近似密度：304×122 父域 D=20 输出 D=20；3/64 会输出 19。此规则已通过实际原生网格和拓扑对照，其他 priority 自动模式未验证。fixture 仅内存修改尺寸，原仓库只读。

完整模型初始图像的 alpha 相同，但 RGB 失败；遮罩与半透明颜色保护尚未实现。不得将上述几何认证扩展成完整静态画面认证。

## P4 静态合成子步骤（进行中）

后续实现和证据见 [P4 记录](p4-static-composition.md)。type=5 单层组合、5-bit mask 覆盖、区域裁切与 icon.attr=1 透明颜色保护已核实。四型号初态和主模型完整身体通过原网页截图；30 组严格图像对照仍有 7 FAIL、10 个轮廓像素。GPU 渲染与几何精度问题已通过独立诊断隔离；未知精度边界与嵌套仍不能记为已验证。上面的初始图像失败是保留的历史记录。

### P6 type=1 配对限幅

对于已验证的对称正范围，圆内值保持不变，圆外将原始二维向量投影至半径为范围上限的圆周。圆外不得预先逐轴截断；`(60,15)` 的方向与 `(30,15)` 不同。Math 输入/输出和最终原生顶点/图像均有独立证据，见 [10 组验证](validation/p6-circle-clamp.json)。此实现仅限幅，不包含胸部/头发积分、初始化或锚点继承。

## Fraise 子同步、细分与透明遮罩

`meshSyncChildMask=1` 对直接子节点的刚性原点求曲面，子节点随后保持自己的局部形状；`8` 将曲面接入后代顶点链，`0` 不传播自身曲面。同域 combine 在 `8` 下合并位移，位置同步下先改变 helper 原点，保留 helper 自己的曲面。Fraise `face_UD/face_LR` 原版及只重定向两处 flag 到同字典 groundCorrection=0 的对照，确认了这些差异；直接对子布局使用 UD+LR 合并曲面会产生 6.668px 的真实错误。

小图自动 division 可截断为零；源 6×7 图仍为 3×3 个顶点，109×219 图为 3×5。显式 division=1 的独立引用 fixture 也为 3×3。修正前缺少的发片一列导致两千多个轮廓差异。原资料中的总 division 最小值不能继续作为公式依据。

Fraise `add_mask` 的 opa=0 不抑制 stencil 贡献。源 mask shader 从纹理 alpha 生成 union，忽略 vertex colour/opacity；颜色路径仍剔除透明图元。原 PNG 与原 WebGL atlas 的 33,554,432 个 RGBA 字节逐位相同；缺失辅助 mask 而非素材解码导致下眼睑残差。未知嵌套/反向语义继续显式拒绝。

四姿态几何闭合，三张静态图和完整 idle24 PASS；head-UD 耳尖一个像素、完整 turn24/additive60 的物理数值仍 FAIL。阶段不完成，见 [证据与冻结输入](validation/p6-child-synchronization.json)。

### 2026-10-04 旋转、叠加与作者归一化精度

源曲面为显式单位 BP 时仍经过逆域 UV 和世界控制点的 Float32 保存，不能作为数学恒等变换跳过。Fraise 待机差分的两级实际锚点记录把这个差异分离出来。物理绝对坐标先乘保存 Float32 的 `1/(double(instance_scale)*double(author_scale))`，再次保存 Float32；弹簧对保存锚点求差分，链条使用当前绝对锚点加保存的初始/外力偏移，禁止以逐步位移累计替代。链条 current rest 使用相加后的 double 临时位移再保存 Float32，最大长度投影从末端沿归一化方向移动超出长度；外部位移仍为作者单位。最新链条证据见 [绝对锚点记录](validation/p6-absolute-chain-anchors.json)。动作过渡的进度缓存保存 Float32，当前曲线和线性值使用保存前的 double 临时进度；`Ro` 第 9 步的 27.000001907348633 与提前舍入得到的 27.000003814697266 可区分。证据和剩余 FAIL 见 [精度记录](validation/p6-transform-precision.json)。
