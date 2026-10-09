# 下一批独立任务

基线：包含[本轮独立复核](agent-review-g19-g21.md)的最新Codex提交。用户要求GLM承担实现、批量运行和证据整理大头，Codex接管求值公式并做生命周期复核；Gemini实际看图。先声明base/文件，保护共享工作；GPU用build/gpu-validation.lock，原素材只读，输入只用自身回调。每阶段验证后commit，真实Agent/Model/Context，报告≤一页，raw放新build目录。

## GLM G25：Codex求值候选的扩大验收（优先）

G23已复核，旧候选失败原因及数量纠正见[接管记录](codex-inheritance-review.md)。Codex已实现单边界挂接公式，候选提交 **d297827** / branch `codex-inherit-analysis` / worktree `experiments/codex-inherit`，27静态几何与5GPU已通过。**不合入主分支，不再独立推导/修改公式。** 主分支15FAIL保持。

1. 从d297827创建自己的隔离分支/worktree；先声明base/改动文件。只开发批量工具、合成契约和证据，保护Codex及旧GLM候选。例：仓库根目录执行 `git worktree add -b g25-acceptance experiments/g25-acceptance d297827`；构建使用Release/WERROR、preview/tests开启、player关闭，运行素材路径指向主工作区assets。公式的测试/说明已在候选提交。
2. 重验候选27静态几何/拓扑，并补其余22实际GPU对照；必须读取实际变量（SL为body_slant），固定与参考相同初态、800×1080、scale .45及居中后originY100。复用已冻结参照须核hash，不补齐旧v5矩阵；所有新结果统一冻结。目标几何≤.001px、标量≤1e-5、顶点/三角形/命令/遮罩匹配，alpha/黑白合成RGB≤2且零超差。
3. 用候选扫描全部195初态，列15旧FAIL的逐型号状态和字段路径，不把成功加载当作视觉通过。检查实际是否存在nested exclusion/其他bm/attr/transform缺口；嵌套仍拒绝，不绕过校验。对新增可求值型号提供对应WebGL初态/代表非零参数参照；原180 CPU回归分别统计，未跑/缺素材Skips独列。
4. 三代表扩组合参数/非零局部平移与旋转、combine、sync1/8、固定步idle/动作/物理恢复、locator/触摸投影，单条件源资产/变量/seed/步长/steps/布局一致；动态数值初态与标量也核实。失败保留最小反例和实际图/trace，交Codex修公式；禁止按型号/节点名补偿、改素材或降阈值。
5. 真实模型预热后检查evaluate/build_frame稳态分配及CPU P50/P95/P99，分别列renderer/application/UI覆盖，不能用合成零分配外推应用。变更工具跑契约测试；GPU在共享flock内串行，无系统输入。参考仓库只读。

交付：`docs/g25-inheritance-acceptance.md`≤一页，raw在新`build/evidence/g25-*`；源码/二进制/资产/工具/参照/结果hash、逐条argv、stdout/stderr与机器信息冻结。入口能复算，非空图检查；工具certified:false不修改。每阶段验证后提交实际溯源，不cherry-pick候选进主分支。Codex复核后决定正式合入；Gemini随后抽看手/腕/衣袖/遮罩边缘，缺失动态仍明确未认证。

## Gemini V17：求值候选视觉抽核

可以先检查本轮 `build/evidence/codex-inheritance-review-2026-10-09/gpu/` 的5组reference/native/diff，再消费G25新证据。逐条件实际看手/腕/衣袖挂接、遮罩边缘、动态连续性，记录真实图路径/hash/输入及观察；像素PASS与目视检查分开。不得按家族外推或调素材/公式。缺图、未运行与被遮挡明确区分；异常给Codex最小复现。交 `docs/v17-inheritance-visual.md` ≤一页，候选仍不正式放行。

## GLM G22：实际Plasma包剩余功能与部署

G19的初态重建/配置页真实创建/首delta0/40轮旧renderer及最终销毁已由Codex修，不重复推倒；见[g19](g19-plasma-report.md)。包probe是预链接native的实际KDE宿主，不能据此宣布干净动态部署已验证。

1. 真正bad→good→bad→good恢复（有效model/session改回，动作继续推进）；分别切换model、session、originY/scale，检查一次批量更新只重建一次，旧state/GPU销毁，new状态/图像/声音能力公开。非local URL、缺配置、非法scale/origin/输入类型应分别到达对应分支，不用坏JSON代替非法配置。
2. 自动实际包入口纳入明确Qt/KPackage opt-in测试；缺素材Skipped。保留四安全负例、新配置9默认+1可选的准确计数及Qt12。pause/hide失败状态/真实输入隔离用自身事件，无系统输入；当前40轮后续Loader恢复真实输入，补每代渲染前明确诊断隔离并验证；记录完成信号首delta0，不能轮询最后一帧当首帧。resize实际containment/FBO尺寸断言、DPI1/1.5；CPU资源/峰值和renderer每代与最终销毁计数分开。
3. 工作区干净可部署stage：明确native库、plugin、qmldir、依赖库、包/schema/UI，不能复制全部build或只装KPackage。提供manifest和生成/清理命令；独立宿主不能预链接native模块，测试从新stage动态import而非build RPATH偶然找到。静态核心Qt重复编入可执行程序/DSO需审查，不增加公共QML计数接口。
4. 只生成可审查的安装/卸载步骤，不安装用户/系统、不改真实桌面或重启shell；已有声音/背景服务保持共享边界，正式应用接入另安排，不复制PCM。

允许adapters/plasma、明确Qt诊断契约、构建/测试和docs/g22-plasma-report.md。Release/WERROR，Qt生成代码例外不扩大；ASan无窗口；每条件raw/截图/完整hash，不能“PNG相同=长期或无泄漏”。正式宿主稳态路径不新增原子诊断/读回/字符串日志；性能结论需单独基准。

## GLM G24 → Gemini V15：真正可消费的触摸视觉证据

G18/G21最终81点有效，不重复写validator。G21汇总有2个旧失败但修正最终42OK；来源文件路径及缺freeze见[g21](g21-touch-handoff.md)，保留失败历史，用新准确索引。

1. 保存直接读外部pixelMarker/presentation的生成入口、source/asset/tool hash与逐字段比较，不复制session/G13区域充当来源。修正42点汇总并绑定实际request/response/hash；当前候选保持PENDING。
2. 为Fraise B/Cinnamon maid-b/Coconut santa-b准备default、idle120、moved、half及真正下身进入画幅的Qt/FBO clean/overlay图。实际FBO尺寸、PNG尺寸/alpha bbox一致，排除旧GLFW2160→1600裁切；覆盖图只用真实区域投影，不自动生成观察。诊断布局独立保存，不覆盖G13冻结布局。
3. 各条件图、原probe、steps/seed/origin/scale/mirror、来源参考、点位request/response/PNG绑定并冻结。新诊断条件不能假冒G13条件；若现有观察工具只接受冻结G13，先建立明确的新捕获manifest入口/对应契约测试，不能移除来源检查或凭hash接纳任意图。新增条件由Codex抽核证据边界。

Gemini V15随后逐图实际观察，填写工具支持的version3（version/observations/observer、逐condition角色/path/hash/reference）；缺图/被遮挡/画幅外/未检查区分。旧lower_body_diag下身UNCHECKED，撤回“网格cutoff”解释。不硬编码MATCH或按家族外推，不根据椭圆数学认证候选，不改素材/工具/公式。视觉任务只交一页docs/v15-touch-report.md及实际证据。

## Gemini V16：Plasma视觉复核

可先看Codex本轮实际包40轮最终图与V13有效裁图，G22后逐条件核对配置/错误恢复/resize/DPI/输入。区分实际包与独立QML三固定图；常态不同时间PNG不用于固定像素判定。实际执行命令、图/hash冻结，不凭同一静态图宣称播放/长期/无泄漏。

## 顺序与Codex保留

**GLM G25→G22→G24**，G25优先扩大已通过代表门槛的新公式覆盖；Codex负责反例修复及正式合入；有明确独立子任务可自行组织，但GPU串行。Gemini V17可先看现有候选5图并等待G25扩展；V16可立即开始、V15待新包；双方先声明共享文件避免碰撞。Codex负责求值公式实现/反例、模块/线程及最终整合，低成本关键修复直接接管；新研究不能仅因收集文件宣布完成。嵌套遮罩/2标量反例、正式声音背景、UI/渐变/reset、195动态视觉与8–24h/多屏/安装仍在路线图，不是本轮已通过项。
