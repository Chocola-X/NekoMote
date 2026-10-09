# 下一批独立任务

基线：包含[本轮独立复核](agent-review-g19-g21.md)的最新Codex提交。用户要求GLM承担实现、批量运行和证据整理大头，Codex做公式/生命周期关键抽核；Gemini实际看图。先声明base/文件，保护共享工作；GPU用build/gpu-validation.lock，原素材只读，输入只用自身回调。每阶段验证后commit，真实Agent/Model/Context，报告≤一页，raw放新build目录。

## GLM G23：15失败型号的继承候选（最高优先级）

G20并未解决支持缺口；先读[g20](g20-inheritance-evidence.md)与C5冻结门槛。正式拒绝保持。保留 `../NekoMote-g20-candidate` 未提交工作，不直接合并或reset；现候选v3与旧报告v1不同，残差有X/Y两个分量。

1. 将当前候选源码patch、base、实际执行命令、binary/compiler/build、输入asset/PSB/texture/browser/tools/reference trace全部hash写入新manifest；先CPU。nekomote_pose --sample-trace实际原生求值对WebGL坐标，不能把复制WebGL的native_trace称原生输出。未达标工具rc0不代表PASS，应核对certified/数值/拓扑。
2. 三代表default及body_LR±5/±15/±30逐条件矩阵；再逐个独立body_UD/SL（实际参数存在才用）。导出每个残差部件的XY、局部/世界矩阵、curve_parent链、变形挂接原点及资源域；完整根链与mesh.bp/cc控制点原值，来源节点路径明确。先完整保存一个小矩阵再扩展，缺证据写NOT_RUN，不捏造结果。
3. 用同一源资产/变量/初态分别验证v1、v3及有公式依据的新候选；量化每种公式在哪个挂接空间失配，禁止按型号/层名补偿或试偏移常数。修正旧“纯X”前提；父世界原点、子局部偏移与资源原点不得混用。新语义只在隔离候选实现。
4. 复用G17扩展195静态来源（234是15型号静态候选，非234实际错误）。CPU候选过门槛才扩大GPU固定初态/单变量/固定步参考截图，并由Gemini抽看手部边缘。失败样例/误差都保留。

验收：几何≤.001px、标量≤1e-5、拓扑全匹配，alpha/黑白合成RGB≤2且零超差；候选源码、命令与结果对应且可重算。未通过仍拒绝，不能宣称15型号可用。允许工具/隔离runtime研究，不改正式求值/素材/ABI放行。交docs/g23-inheritance-report.md一页+矩阵，Codex独立验公式/代表再整合；大量采样不交Codex手跑。

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

**GLM G23→G22→G24**，优先解决用户仍不能选择的15型号；有明确独立子任务可自行组织，但GPU串行。Gemini V16可立即开始、V15待新包；双方先声明共享文件避免碰撞。Codex审关键公式/模块/线程及最终整合，低成本关键修复直接接管；新研究不能仅因收集文件宣布完成。嵌套遮罩/2标量反例、正式声音背景、UI/渐变/reset、195动态视觉与8–24h/多屏/安装仍在路线图，不是本轮已通过项。
