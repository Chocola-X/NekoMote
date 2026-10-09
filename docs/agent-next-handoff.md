# 下一批独立任务

基线：包含[2026-10-09复核](agent-review-2026-10-09.md)的最新Codex提交。声明实际base/文件，阶段验证后提交真实Agent/Model/Context；摘要≤一页，大图/日志放新build目录。原素材只读，GPU共享锁。用户要求GLM承担实现/批量运行大头，Codex集中风险抽核。

## GLM G19：Plasma包配置、生命周期与部署验证（优先主任务）

已有动态模块+真实WallpaperItem+main.xml/config.qml，实际隔离KPackage/Containment宿主已显示Chocola/Fraise；不要重新实现它们或声称SDK缺失。

入口：`cmake --preset plasma && cmake --build --preset plasma`。
`nekomote_plasma_package_probe MODEL SESSION NEW_OUTPUT_DIRECTORY`通过实际KDE库加载包，用workspace临时XDG状态；`nekomote_plasma_probe --help`是确定步/常态/软件后端诊断组件入口。后者不是实际包，报告分开。

1. 完善package_probe和自动Qt/KPackage检查入口：实际配置页/schema加载、改model/session/origin/scale引发一次受控组件重建，旧资源销毁后计数平衡；缺配置/坏资产/非法配置/错误后恢复有公开诊断、GUI存活。配置值不能靠改私有renderer/session状态注入。
2. 对实际包补受控pause/resume、隐藏/再显示、resize、连续创建/删除20轮、DPI1/1.5、配置切换20轮。动作确实推进、暂停不推进、恢复首delta0、steady无常态读回，记录每轮CPU/GPU所有权/峰值/最终计数。资产缺失显式Skipped，不把普通窗口矩阵当作实际包验收。
3. 显式资源定位/完整build部署清单与可重复暂存命令：动态插件、依赖库、qmldir、包/schema/配置页，不能复制整个build目录，也不让旧STATIC产物混入。暂存仅workspace，先给出可审查安装/卸载命令，不安装用户/系统、不改桌面配置或重启shell。
4. 配置界面应易用：文件选择/有效本地URL、origin范围及模型缩放、明确加载/能力错误；不暴露内部步骤计数/ABI字段。使用已有会话，不复制声音/背景/PCM策略；正式声音/背景接入另有C6门槛，当前prototype可显式静音。
5. 测试用小型合成fixture覆盖配置拒绝/输入类型、真实资产只做显式GPU集成；默认discover及ASan无窗口。Qt生成代码的warning例外不得扩大到正式源码。正常调度计数不能写成呈现FPS。

允许改：adapters/plasma、Qt桥的明确宿主/诊断契约、对应CMake/测试、docs/g19-plasma-report.md；不改求值/GPU公式/ABI签名。验收：Release/WERROR、已有Qt12/四安全负例保留；新增实际包的上述分支有raw/hash；三固定QML/WebGL门槛不退化；不宣称已修改真实桌面。先核心小包通过再批量。

## GLM G20：0x7fc继承的准确参照与隔离候选（继G19）

当前正式拒绝保持。阅读G17新账本、c5-mirror-mask-handoff、历史030512d/a814cbd/a858f01及手部缺陷记录。新G17的234项是静态候选，不能直接作为234个实际错误。

复用CPU审计扩展195静态覆盖；从15FAIL选Coconut、Azuki、Cinnamon三代表，提取完整腕部父子链、源字段/资源域/mesh.bp/参数/transform顺序、实际拒绝位置及原JSON来源。不要只看日文节点名。补probe公开错误节点诊断可以做，但错误码/来源不能丢失，不擅自放宽正式拒绝。
用现有reference_capture取原PSB/WebGL独立初态、固定步、逐变量单独偏移后的拓扑/网格点/屏幕坐标与PNG。GPU锁、冻结源码/PSB/texture/browser/tools/binaries，失败全部保留；物理隔离且初态一致，不复制浏览器私有核心。
提出能解释全父链组合的独立数学候选，先在隔离worktree/离线工具验证；不提交生产路径放行、型号名分支、标签判断或approx fallback。门槛沿C5：几何≤.001px、拓扑全匹配、标量≤1e-5、alpha/黑白合成RGB≤2且零超差。旧“少拉长一些”不能通过。
交付docs/g20-inheritance-evidence.md一页+机器矩阵/最小样例/公式依据；未过门槛保存反例。候选通过也先交Codex独立复核，再迁入正式核心。研究和大量采样由GLM承担，Codex不逐条手工跑模型。

## GLM G21 → Gemini V14：真实触摸/来源证据

严格G18工具已由Codex修复，GLM运行冻结工具，不再改写复杂验证器或生成观察结论。
首批Fraise B、Cinnamon maid-b、Coconut santa-b：默认、idle120、移动、半缩放原图/覆盖图，及使下身进入画幅的独立诊断布局。优先Qt/FBO取图，原2160 GLFW图恰截1600不作为角色边界；不覆盖G13旧布局。
依据Gemini实际图像测点，在独立初态执行边缘/近空白/重叠/优先级点；每点PNG/中心输出/作者坐标转换、mirror、origin/scale、steps/seed、真实request/response/hash齐全。idle120在121触摸；X250等未执行点要实际补测，不复制到另一姿态。当前候选只存建议，不改素材/资产。
建立独立source-field或执行WebGL参考：直接读源字段而不是复制导入session/G13区域；Y向下、作者→中心输出→PNG公式一致，Fraise胸357.5中心/897.5 PNG。明确参考来源类型，不把source重算称WebGL实测。
交付新包/docs/g21-touch-handoff.md、固定工具hash及每条件原始文件；缺证据不通过。候选验证器当前保持PENDING，不能根据椭圆数学或prefix宣布建议配置通过。

Gemini V14仅使用图像工具实际观察/测点并填写version3逐condition证据，区分可见、被遮挡、画幅外、未检查；三重点型号先行，再逐目录。3旧记录均PENDING；不能自动生成MATCH/MISMATCH或从家族外推。不给自动账本数字写测试。交docs/v14-touch-report.md一页，不改工具/公式/资产。

## Gemini V13：实际Plasma包视觉

可立即查看本轮实际包Chocola/Fraise及Qt/WebGL三固定图；G19交付后核对配置页、错误显示、DPI/resize、暂停/恢复和受控输入。采用实际Plasma KPackage宿主，普通Qt组件单独标记；原实际包截图为常态不同时间，不能直接作固定像素结论。先保存真实逐条件全图/裁图/hash，测试输入只用自己的回调，不注入系统事件。

## Codex保留

新语义/ABI/线程与最终整合；便宜关键修复或反复错误实现接管。G19/G20/G21的大实现、采样、测试整理交GLM，图像观察交Gemini。正式声音/背景、UI/渐变/reset、195动态视觉、8–24h/多屏/安装为后续退出条件，不因原型显示而宣称最终成品。
