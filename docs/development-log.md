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
