# 下一批独立任务

基线：包含[本轮独立复核](agent-review-g19-g21.md)的最新Codex提交。用户要求GLM承担实现、批量运行和证据整理大头，Codex接管求值公式并做生命周期复核；Gemini实际看图。先声明base/文件，保护共享工作；GPU用build/gpu-validation.lock，原素材只读，输入只用自身回调。每阶段验证后commit，真实Agent/Model/Context，报告≤一页，raw放新build目录。

## GLM G25：Codex求值候选的扩大验收（已完成）

G25已由GLM-5.2执行完毕，报告见[g25](g25-inheritance-acceptance.md)。27静态几何/27GPU/195初态/45扩组合/性能基准全通过，证据冻结在`build/evidence/g25-acceptance/`。12旧FAIL型号无WebGL参照仅验证加载；sync/动态/locator/触摸未测（无浏览器/动态工具）；renderer/UI稳态分配未测。交Codex最终复核决定正式合入。Gemini V17可消费G25 GPU证据抽看手/腕/衣袖。

## Gemini V17：求值候选视觉抽核（已完成）

V17已由Gemini 3.8 Flash执行完毕，报告见[v17](v17-inheritance-visual.md)。消费Codex 5组与G25 22组共27组GPU渲染图，手/腕/衣袖挂接消除8.95px断裂撕裂，bodyLR连续性条带与SL30/UD30目视全部通过，证据冻结在`build/evidence/v17-inheritance-visual/`。12旧FAIL无WebGL参照仅加载通过，扩组合新条件无浏览器参照标Skipped；主分支候选仍保持正式拒绝，交Codex最终复核放行。

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
