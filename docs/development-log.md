# 持续开发记录

## 当前目标与验收顺序

用户授权：审查 P0/P1、必要时重构、完成原生二维模型引擎；各阶段验证后提交。必须有原 WebGL 图像对照，不能以构建成功代替画面正确。

1. 复查 P0/P1：新机器构建、真实素材回归、错误输入、信息完整性与模块边界。
2. 修复导入数据丢失与错误契约，建立目录审计及字段证据。未经画面对照的语义标为候选。
3. 建立受控 WebGL 参考采样；固定分辨率、变量、初态、时间、物理设置并保存来源 hash。
4. P2：纹理/几何/透明合成、单部件诊断、截图、GPU 生命周期与基线。
5. P3/P4：参考实验确认参数、变形、继承、层次和遮罩；复杂模型完整静态图像通过后提交。
6. P5/P6：时间线、差分、物理与交互；按相同初态固定步长做动态对照。
7. 按路线图继续性能与部署；各阶段退出条件未通过时保持未完成状态。

## 2026-10-03 接手审查

- 起点：`d5fd1a1`。已有 7 个未提交文件，涉及用户迁移机器的路径/文档修改及 CTest 工作目录修复；保留。原始 diff 保存到忽略的 `build/evidence/user-migration.patch`。
- 同级 `../NekoWebShow/` 存在。真实样例路径为 `model_conversion/chocola/chocola-date-a/`。
- 当前 GPU：NVIDIA RTX 3060 Laptop，驱动 575.64.03，X11 `:0`，OpenGL 4.6；旧交接里的 RTX 3080 Ti 数据只代表旧机器。
- `cmake --preset dev`、构建及包含真实模型的 23 个单元测试通过。
- **P0 缺陷**：关闭预览/测试仍 FetchContent 配置 GLFW/Catch2，使核心构建依赖显示开发库。改为按目标开关配置。
- **P1 严重缺陷**：motion 级 parameter/priority、layer.type、content.angle/zx/zy/motion、遮罩组合引用及时间线等字段丢失；parameterize 的 null 被变成 0；畸形 bp 被当作无 bp；resx/引用不完整仍成功；解析错误可能抛到宿主；CLI 不输出当前资产。
- **P1 证据缺口**：未提供 195 条审计；Bézier、partsOrder、默认变量等结论没有受控图像证据。旧“完成”状态不成立，应修复并重新验收。
- 不回退 Git：模块边界和数学基础仍有效，直接演进可保护用户工作且没有维护双套接口的必要。

## P0/P1 修复验证

- Debug / ASan+UBSan：28/28 通过，含主模型和错误输入、状态保留、资产往返及拒绝覆盖回归。
- 195 条目录重新审计：193 结构导入通过；milk-winter 缺 JSON；vanilla-lolita 图集 icon 越界。外置资源映射、别名和重复目录选择有报告。
- 原 WebGL 主模型参考图已经捕获并目视核实；关闭物理/风、冻结时间、记录 hash 与 GPU。默认图及单独口部图在 build/evidence/reference/。
- P1 只认证结构导入，未知物理/控制字段完整离线归档，不认证播放。后续逐项规范化并重新导入，不让原 JSON 进入热路径。
- 提交前从暂存区导出独立源码副本做无窗口构建：不配置 GLFW/OpenGL，28/28 测试通过；命令日志在 `build/evidence/p1-core-{configure,build,tests}.log`。该复核提交不混入在工作区开发中的 P2 图形代码。

## P2 验证

- Debug/Release 31/31（30 个单元入口 + 3 个用例组成的 GPU 入口）；ASan/UBSan 30/30。
- 主模型口部在 1×/2×/0.5× 与透明度 0.5 的原 WebGL 对照通过；alpha 最大 1/255，黑/白背景 RGB 最大 1/255。已目视查看放大并列图。
- 修正参考隔离过滤以保留原驱动颜色保护双遍绘制；缩放透明边缘使用 alpha 与实际背景合成度量，同时保存原始直通 RGBA 最大差。
- GPU 生命周期、host FBO、resize、顺序与零 C++ 稳态分配实测通过。发布态基线见 docs/p2-handoff.md；不等同完整模型性能。
- 下一步以完整源资产创建 P3 候选求值，逐项验证；当前仍未达到复杂模型 M1。

## P3 控制与变形实验

- 已验证 24 组 `chocola-date-a` 固定输入与另三型号的默认/组合姿态，共 30 组原 PSB 顶点对照，阈值 0.001px，最大 <0.0003px；记录 `docs/validation/p3-geometry.json`。
- 发现组合偏差源于两项独立问题：meshCombine 是同域控制点位移叠加；clampControl type=0 是保持方向的三角函数半径映射。通过内存测试 fixture（仅六个 enabled 字节修改、记录 hash）隔离，然后通过原 PSB 复核，未修改原仓库。
- Schema 2 加入 paired_clamps，schema 1 明确拒绝重建。增加无图形依赖的 C++ runtime 数值内核和冻结参考值回归；离线场景 probe 不进入播放/渲染输入。
- P3 仍进行中：接下来完成 C++ 静态场景编译/参数求值，核实细分/部分继承/资源切换；之后 P4 遮罩与完整原生截图。Python 几何吻合不能替代 C++ 场景/图像通过。
- 本次复现入口从新目录重做 30 组全部通过；Debug 34 项（含 GPU）、ASan/UBSan 33 项核心检查通过；当前导入器重新审计 195 条仍为 193 可结构导入、2 个既有素材问题。新增内核尚未进入播放热路径，未声称完整引擎性能变化。
- 细分新证据：非零 meshDivision D 的可见图像，X 段数候选为 `floor(D*w/(w+h))+1`、Y 为 `D-floor(D*w/(w+h))+1`；D=16/20、长宽比不同的图像已有一致顶点数。D=0 且 objTriPriority=2 的 cheek 自动划分仍不明，不能任意用默认 D 替代。

## P3 原生固定姿态场景

- 加入无窗口/JSON/GL 依赖的 GeometryScene/GeometryInstance；引用和 ID 加载时解析，实例状态独立，预分配求值/构帧。
- 30 组原网页对照扩展到实际顶点/UV/三角形。发现并修正 D=0 自动网格密度依赖活跃父域的遗漏，四型号全部通过，最大 0.000637226px。
- 只读 PSB fixture 新增显式图像尺寸修改、记录每个字段和来源 hash；213 平方边界排除固定密度候选。默认仍使用未修改原 PSB。
- 原生诊断截图已生成并目视检查：alpha 完全匹配，RGB 仍失败，瞳孔遮罩/透明颜色保护待 P4，不宣称 M1。
- Debug/Release 39 入口、ASan/UBSan 38 核心入口通过；预热后 200 次资源切换/求值/构帧普通 C++ new 计数为零。Release CPU 求值+构帧 P50/P95/P99=0.628749/0.655709/0.719409ms（固定输入 2000 次），范围和硬件见 docs/p3-native-scene.md。
- P3 保持进行中；本次提交是已验证的几何子步骤，下一步遮罩/合成。

## 2026-10-04 P4 遮罩/颜色保护子步骤

- 加入 source-independent mask/保护合成协议、RGBA8 中间目标、5-bit mask scratch；union、每次清空、源/绘制区域交集及 2 像素边界通过观察确认。显式 resize/reserve，RAII 与热路径无普通 C++ new 已测试。
- 原 WebGL 上传 type=4444 不等于实际附件位数：当前 NVIDIA/ANGLE mask 为 5/5/5/1，直接查询及 32 档 green 读回确认；原生 RGB5_A1 并查询 green=5。改正先前误用实际 green=8 的目标后，四型号初态通过。
- 局部网格与 atlas packing 解耦，canonical identity BP 保持精确坐标；Bézier double 基函数减少 Float32 累加舍入。不同域精度、系数、controller 与 placement 候选无法统一闭合，已移出实现，无按型号/姿态切换公式。
- 新 static 验证入口重导入四型号，校验原 PSB/驱动/资产/PNG hash、输入、物理和 fixture；全部 30 组几何通过（最大 0.000251655px），严格图像 23 PASS/7 FAIL，共 10 个像素 RGB 达 3/255。保留 2/255 门槛与全部 FAIL，不将阶段标完成。
- 1200×2000、0.65 倍的完整可见身体通过：alpha=0、RGB 最大差 1/255；与原项目 BG_29A 的离线合成也通过，已目视检查并列图。背景尚未进入 application。
- GPU pipeline 隔离使用参考几何得到合成 RGB/alpha 零差；位置/UV 替换实验将残差定位为几何。所有此类诊断输入只留忽略的 evidence，不用于运行时或预览。
- 未验证 frame.type=1 选中时明确拒绝；回归覆盖未知父层变换不能静默影响子 sprite。正常上一帧也不会在失败后泄漏。
- 阶段子步骤提交前 Debug/Release/ASan 检查及基线见 docs/p4-static-composition.md。P3/P4 继续进行中；下一步核实轮廓计算精度、嵌套与反向遮罩，继续时间线的独立语义采样。

## P5 独立语义调查

- 修复参考采样的物理声明：scale=0 不停止积分；添加显式控制器 fixture、逐步变量、额外物理绑定、主/差分播放和直接变量过渡诊断。原 PSB/外部驱动只读。
- 六组 easing 原始轨迹冻结回归通过（归一化容差 5e−7），Debug/Release 43、ASan/UBSan 42 项通过。
- 新采样工具零步默认 PNG 与既有基线完全相同；九字节禁用物理/眨眼 fixture 的 idle 120 帧也完全相同。
- 发现变量关键帧启动目标过渡、更新分区敏感、SDK loop/duration 查询不可靠以及表情控制图不能省略。详见 docs/p5-animation-investigation.md。下一步规范化控制数据并实现作者固定时钟；P3/P4 与完整动画仍未完成。

## P5 原生动画与 schema 3 子步骤

- 将控制元数据规范化至 schema 3，四型号已有 model 字段逐字段未变，静态矩阵仍 23/30 图片 PASS、10 个失败像素。只支持当前 schema，旧资产需重新导入。
- 原生动画 Program/Instance 接入主动作、线性表达式路径、并行/待机差分和 60 Hz 作者时钟；实时预览与固定步进截图使用同一路径。
- 18 组冻结数值轨迹和状态回归通过；Debug/Release 48、ASan/UBSan 47 项，预热后 500 步零普通 C++ new。
- 原生动态 7 组全部几何通过，严格图片 2 PASS/5 FAIL，共 18 个像素 RGB 达 3/255；已经目视核实并列图，不放宽门槛。
- 确认 blinkEnabled=0 同时改变 Eye 过渡行为，调整 Eye 实验使用仅物理隔离的参考。非线性跨段和选择器仍明确拒绝。详见 docs/p5-native-animation.md。

## P6 物理配对限幅子步骤

- `clampControl type=1` 是硬圆形限幅，与 type=0 的曲线半径不同；必须先保留完整原始向量方向，不能先逐轴截断。`(60,15)` 的原 WebGL 数学观察与几何排除了错误候选。
- 胸部四个绑定在 10 组输入的原生几何/UV/拓扑全部通过，最大 0.000065186px；截图全部通过 2/255 门槛，alpha 无差，RGB 最大 1/255，已目视检查并列图。证据见 `docs/validation/p6-circle-clamp.json`。
- 上述隔离只关闭八个物理 enabled 字节，以允许手工指定物理绑定；它不认证积分器。未修改 PSB 的实验会将这些手工物理值恢复为控制器输出，已记录原始失败结果。更新采样工具后，原 PSB 零步默认 PNG hash 仍为 `e23aa19c310b9f0f069d72ed00324eca8d2e2f5cca5427f936fa851f4585a793`。
- 采样工具新增显式 hair/parts/bust SDK scale 输入，默认保持全零。120 步未修改 PSB 观察显示 scale=1 的静止模型物理绑定全零，而 scale=0 的胸部 UD 从第一步即为 -18.189874649；scale 不能当作禁用开关或简单输出倍数。移动模型从第二步开始产生响应，下一步核实锚点和积分。
- Debug 49 项、ASan/UBSan 48 项检查通过。限幅只增加常数空间数值运算，无动态分配；尚不宣称完整物理或播放性能。

## P6 弹簧内核与受控物理事件

- 用元数据数值/字符串的同长度 PSB fixture、逐步外部位移事件、逐步 atan/sqrt 观察隔离物理更新。所有地址表/资源与外部素材只读，非法范围/字符串/编码宽度明确拒绝，新增工具零步 PNG 仍与旧基线 hash 一致。
- 原生 SpringState/advance_spring/spring_output 无 JSON/GL/宿主依赖；验证重力/弹簧/摩擦顺序、幅度减 ofs 的顺序与配对死区。四型号相同 atan 单位映射；12 组冻结轨迹、535 条数值断言通过。
- 原生内核计算参数后经正常场景/GL 输出，四个 24 步弹簧隔离截图全部 PASS，alpha 无差、RGB 最大 1/255，目视核实；不把 fixture 验证记为未经修改的完整播放验收。证据 `docs/validation/p6-spring-images.json`。
- Debug/Release 51 个入口、ASan/UBSan 50 核心入口全部通过；预热后的 1000 步无普通 C++ new，独立状态重置一致。发布态单粒子 step+map 中位 16.834 ns/op，仅数值内核。
- 父级累计 Float32 变换候选更贴近锚点位置，但 30 组静态图片仍 23 PASS/7 FAIL，未直接替代正式几何。完整 locator/变形继承、头发两粒子约束、物理 schema 和实时会话待接入，详见 `docs/p6-physics-investigation.md`。

## P6 限定场景锚点子步骤

- 提供由场景持有的导出标记 NodeId 与实例求值后锚点查询；活动/非活动分支、失效和未验证父层继承明确区分。只对 full inheritance 和已验证零局部原点 type-one 0x200020c 开放，不推断一般部分继承。
- 胸部锚点八组真实 body/head/act_sp 变形输入与原 SDK 逐步物理位移观察全部通过，最大 0.0000658795px，证据 `docs/validation/p6-locators.json`。
- Debug/Release 52 项、ASan/UBSan 51 项全部通过；200 次切换/求值/查询无普通 C++ new。重做四型号 30 张静态截图仍 23 PASS/7 FAIL，几何全 PASS，没有新增画面回归；默认并列图已查看。
- 头发一侧伸长弹簧/第二粒子最大长度约束候选已与隔离数值接近，弯曲输出调查继续；尚未把候选或原 trace 数据接入正式播放。
