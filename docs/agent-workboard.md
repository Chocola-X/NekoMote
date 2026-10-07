# Agent 协作任务

更新：2026-10-07。当前结论见 [整合摘要](review-agents-current.md)，阶段见 [ROADMAP](../ROADMAP.md)。旧G2/G4/G6/V3/V5任务已执行过，不再按原文重做；下表区分产出和验收。用户在本轮之后独立启动另外两个Agent，本文件不表示已通过工具启动他们。

## 当前状态

| 范围 | 状态 | 后续 |
| --- | --- | --- |
| Fraise add_mask、代表矩阵 | 上一轮115/115独立验收，已闭合代表范围 | 嵌套遮罩另由Codex处理 |
| ABI0.3.0主边界、C++候选事务 | headless、长度/异常/帧失效及失败保留已有证据 | 真正宿主生命周期由Codex处理 |
| GLM G4 | catalog/独立状态列已实现；文件保护缺陷由Codex修复 | G7完成账本和负例 |
| GLM G2/G6 | C宿主已精简、容量getter可保留；确定性截图/输入及构建矩阵未验收 | G8/G9 |
| Gemini V3 | 两组源GPU混合证据可用于研究，未认证全姿态 | V6补条件；原生映射由Codex |
| Gemini V5 | 独立8组状态测试通过，静态截图已交付；动态视觉未验收 | V6修正报告/补证据；输入隔离由Codex |
| 当前启动 | milk-summer bm19严格拒绝；错误信息及首帧预检已修复 | Codex优先正确实现混合，完善恢复体验 |

## 文件所有权与协作

- **CodeArts / GLM-5.2：G7 → G8 → G9。** 适合明确契约的工具、负例、诊断宿主和构建整理。无限额度优先用于这些独立工作，不据此扩大到未知数学语义。
- **Antigravity / Gemini 3.8 Flash：V6。** 负责受控截图、WebGL数值观测和报告，本批先不改产品UI实现。允许修改 `tools/reference_capture/trace.js` 和新增限定采样工具；不改main、runtime、render_gl、ABI及GLM文件。
- **Codex：C5/C6/C7。** 负责未知求值、GPU混合/遮罩、应用时钟/声音/输入和真实宿主整合；不等待其他Agent完成不相干任务。
- 需要共享入口时记录最小接口及输入输出，由所有者实现。不得改第三方私有asm核心；源仓库保持只读。截图及长采样以 `flock -n build/gpu-validation.lock` 串行；采样先冻结二进制/hash。
- 每个小阶段验证后commit，正文含Agent/Model/Context；只更新自己的交接报告及development-log。当前状态文档由Codex整合，其他Agent不得自行把跨模块范围标为完成。失败证据保留，不修改阈值。

## GLM G7：可靠目录账本与导入负例

范围：`tools/import_all_assets.py`、`tools/import_all_test.py`、目录选择的小型共享模块及对应Python测试；可协调 `audit_catalog.py` 调用共同选择函数，不改其CPU求值实现。

Codex已实现：已有目标包括partial/旧schema/文件/链接一律保留并以EXISTS_UNCHECKED拒绝；同文件系统暂存及Linux禁止覆盖的原子发布；CPU报告不写源目录，CPU FAIL使整次退出非零。保留这些契约，不恢复SKIP冒充验证或删除重建。

交付要求：

1. 三个目录入口共用同一个canonical选择函数，索引每批只建立一次；歧义、缺失和非法catalog ID显式拒绝。账本保留全部195条产品项，结构/CPU/视觉/交互/soak分别统计。
2. 每项I/O、导入器及会话工具失败/超时均保存节点、命令与原因并继续生成完整账本；中断时有原子检查点。报告保留CPU详细结果及输出hash/当前schema；未运行状态不能填OK，结构OK不代表会话或画面可用。
3. 新增小型fixture覆盖歧义、缺resx/纹理/会话、旧schema/partial/额外文件、竞争创建目标、导入/审计失败和超时；全部源路径只读。复用已有5项保护测试，不为已有目录添加隐式修复或兼容回退。
4. 使用新的输出目录验证，不覆盖 `assets/characters`；不重导入冻结115组资产。195条仅结构/CPU账本，不宣称视觉通过。

验收：Python负例能到达实际分支；真实一条新导入、重复拒绝覆盖、195项账本完整；失败非零，原素材/旧输出/额外文件hash不变。文档放 `docs/g7-handoff.md`。

## GLM G8：确定性C ABI诊断工具

范围：`apps/abi_host/*`、新的宿主测试/交接报告。只调用既有ABI及容量getter，不改公共ABI或复制声音/壁纸策略。

1. 明确 `--steps` 是作者步还是显示帧；固定步截图使用固定1/60秒、seed、初态与坐标，不能用墙钟声称确定。交互模式与固定截图模式分开，截图不能无限等待。
2. 数值解析完整拒绝尾随字符、负数、溢出和超界；路径不得静默截断。纹理加载/上传、截图分配/I/O失败立即非零，不能继续成功。用缺纹理的有效合成资产验证，不能只测缺模型。
3. 截图编码与文件说明一致；若交付PNG就输出可解码PNG。在明确有效绘制目标上读回，避免交换缓冲后读取不确定内容；截图不进入稳态/性能入口。
4. 两次同输入像素相同；与相同放置/背景/步数的C++诊断输出比较，保存RGBA/黑白合成及来源hash。覆盖错误退出和清理，禁止OS键鼠注入。

验收：实际成功与负例、同输入重复截图及相同条件对照；明确仍是静音C ABI诊断程序，不是Plasma壁纸。文档放 `docs/g8-handoff.md`。

## GLM G9：检查入口与构建矩阵

范围：`apps/player/CMakeLists.txt`的测试登记、构建/检查工具与 `docs/building.md`；不改UI状态/渲染实现。把独立UI测试注册CTest并标player_ui，仍独立报告数量；加入批量工具Python检查入口。复核无ABI纯CPU、headless ABI/ASan、preview开/player关和完整player；GCC15无ABI警告需保存最小复现并解决，不扩大禁用。源码副本无assets/refer时，真实素材集成必须Skipped，合成项执行。文档放 `docs/g9-handoff.md`。

G5同条件性能方法延后，不与以上工作同时扩大范围；UI稳态优化需profile后另派。

## Gemini V6：补齐可复核的视觉证据

范围：新的 `docs/v6-visual-report.md`、参考采样工具及 `trace.js`；不要覆盖旧截图/FAIL，不改求值或产品UI。

1. 修正V3/V5报告：标明每个结论对应父提交/当前diff；拖拽前置、global_alpha、.28/.24秒在97c9409之前已存在，不算本轮修复。状态测试计算标题偏移不证明实际图元0px误差；原生6/3是Playback/Geometry，不能标成并不存在的ABI错误枚举。
2. 源GPU每个选定draw记录colorMask、blendEquationSeparate、FBO、shader/uniform、有效opacity、变换和图元定位。分别采样bm19/attr2的初态及可独立控制的其他姿态/alpha条件，记录参数不影响该层时的局限。坐标/seed/步数/来源hash和可复跑命令完整；无相应样本就保留未知，不能推断全范围。
3. 检查已有静态4态截图与对应WebGL UI参照；动态拖动/渐变需要应用内确定输入及逐帧截图。现有入口不足时报告最小所需回调，由Codex补；先完成独立源GPU/静态工作，不用系统输入、不自行改main、不声称动态已验收。
4. 给出具体PASS/FAIL/未测项和局部裁切，不只写“风格一致”。本轮不再次调查已交付的Coconut手部V2；原生映射未完成之前不填新型号视觉PASS。

## Codex下一阶段

- **C5：** 优先bm19/attr2独立映射及真实几何/像素回归；继续0x7fc与嵌套遮罩。门槛不变，不添加近似模式。
- **C6：** 默认角色加载的恢复/能力提示、UI注视隔离、角色线性渐变与共同时间、背景reset/重入、声音候选失败和资源回收；提供最小UI回放诊断入口。
- **C7：** 当前窗口应用稳定后做最小Qt Quick宿主GPU/线程/context/resize/DPI验证，随后实际Plasma插件。完整195视觉/交互、8–24小时、多屏生命周期与安装包仍是最终交付条件。
