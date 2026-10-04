# P5 原生动画子步骤（进行中）

2026-10-04。已接入原生主时间线、已验证的表情/Eye 路径及自动眨眼、并行与待机差分、60 Hz 作者时钟及普通窗口。仍是显式诊断；完整动画/物理播放器未认证。P3/P4 既有精度和嵌套验收也未闭合。

## 数据和运行时契约

当前资产为 schema 5；最初在 schema 3 加入强类型 variable_controls，保留区间/连接点、眼/口型/选择器规则。运行时不解析 source.json，不根据变量名或 instantVariableList 猜测行为。四型号除新增控制数据外与 schema 2 model 对象逐字段相同，迁移证据见 docs/validation/p5-schema3-migration.json；必须重导入旧 bundle，无兼容读取。

AnimationProgram 编译控制图/轨道与 GeometryScene 的参数 ID，拥有全部只读数据；原资产与几何场景不必继续存活。AnimationInstance 借用 Program，后者须更晚销毁；独立实例不共享变量、播放游标或差分状态。所有操作同一求值线程。

帧路径复用全局基础通道、各差分轨道及输出数组。主动作停止/替换后，已经启动的变量过渡仍向原目标推进，停止只取消后续事件。差分在独立通道求值，每帧从基础值加一次，避免累计漂移。0=替换、1=并行主动作、3=并行差分；资产 diff 字段不代替显式播放模式。

关键帧 type=2 启动目标过渡，duration=next_time−time−1；type=0 终止轨道。只支持经验证的整数作者帧、终端与循环形态，未知类型、非整数时刻、选中的未验证控制器明确失败。主时间线与差分循环首帧的更新边界不同，差分在回绕帧不推进通道，下一帧恢复；真实 420/421/422/430 帧几何对照支持该规则。

表达式路径先减少区间跳转，再选择靠近目标的连接点，不能按全局数值距离最短路径拟合；跨区间在当前作者步跳转并推进新段，不保留跨界余量。目标区间的内部不作为连接点路径，避免先进入区间再折返。线性路径已有 11 组回归；新增 35→5 线性及相邻起点确认目标区间接入规则。Eye 跨区间规则见下方最新子步骤。

非线性 Expression 独立保存 Float32 曲线距离，通过逆缓动还原进度，加 Float32 的 1/duration，再计算正向缓动。当前位置消费保存前的 double 位移；不能由当前位置倒推曲线缓存。跨段后将累计段长减本步临时位移保存为新距离，再于同一步计算新段位移。初始/末尾零距离连接点分别对齐第一段/最终目标。九组数值轨迹覆盖正反向、非零起点、中途改向、正/负/分数缓动；七组未修改源素材的完整数值、最终几何/拓扑与图片全部通过。两帧跨段使网页参考产生 NaN，原生在对应作者步明确失败并让 PlaybackInstance 隐藏旧帧，重置后可恢复；不能用钳位制造已还原的正常过渡。证据见 [非线性表情验证](validation/p5-expression-easing.json)、[完整输入矩阵](validation/playback-expression-inputs.json)。

裸变量与已禁用的变量控制器在请求时直接赋值；启用控制器按自身规则覆盖。Mouth 的 talkLabel 是独立插值通道，不按裸变量处理。不能从 instantVariableList 推导整个模型的策略。Eye 的手动过渡与自动眨眼开关分开；自动眨眼调度已按下面的固定种子证据集成。

当前作者时钟由 PlaybackInstance 统一管理，AnimationInstance 只推进一次作者步。作者时钟使用双精度秒累计，在 60 Hz 内步进，显示 FPS 不改变事件顺序。单次 catch-up 限 15 步（250 ms），丢弃超量 elapsed；暂停/恢复清除余量，不追赶休眠期间历史。PlaybackInstance::step() 暂停时也不推进。调用者提供单调时间差。

## 对照与当前限制

18 组冻结 SDK 变量实验覆盖主动作、替换后已启动过渡、并行独立动作、绝对循环、表情路径、口型即时请求及 sample_05。容差 1e−5 原变量单位；数值与播放状态通过。初始 Play 会清除 SDK GetVariable 结果缓存，但不代表实际状态变为 0，因此轨迹从第一次 Update 比较，初态另外用真实几何/图像验证。

新增实际原生动态截图矩阵保留 0.001px 几何与 2/255 图像阈值。7/7 几何/UV/拓扑通过，最大 0.000367481px；严格图片 2 PASS/5 FAIL，共 18 个像素，RGB 最大 3/255，alpha 最大 1/255。已目视查看 sample_05-14 与 additive-422 的原/新/16 倍差异并列图。FAIL 详见 docs/validation/p5-dynamic-images.json；不以数值测试替代画面通过。

源物理隔离用 8 个 enabled 字节 fixture。2026-10-04 字段别名审计纠正此前解释：该 PSB 的 blinkEnabled 与 Eye enabled 共享标量，早期九字节 fixture 同时关闭了整个 Eye 控制器；并非 blinkEnabled 本身改变过渡。17 组旧轨迹的实际输入范围已更正，原数值、PSB 和截图 hash 保留。只关闭眨眼、保持 Eye enabled=1 的独立地址引用 fixture 与正常 Eye 具有相同的 14 步渐变，第 7 步原生截图通过 2/255、几何通过 0.001px。工具、输入与证据见 docs/validation/p6-fixture-alias-audit.json、p6-eye-controller.json。原 PSB、外部驱动与原仓库文件均只读。

重新导入 schema 3 后，原静态矩阵仍为 23/30 图片 PASS、10 个失败像素，全部几何通过；没有新增静态回归。当前输入与资产/来源/截图/trace hash 分别冻结在 static-inputs.json、animation-inputs.json，历史阶段记录不覆写。

## 验证与复现

Debug/Release 48 个 CTest 入口（含 GPU 入口）、ASan/UBSan 47 核心入口通过。新回归覆盖 30/60/144 Hz 分区、暂停/超量 catch-up、重置、实例状态、未知 ID/控制器；预热 120 步后的 500 步动画、图路径和几何构帧普通 C++ new 计数为零。

```sh
build/dev/bin/nekomote_preview --animation-diagnostic --asset /path/to/model.nkm.json \
  --origin 400 940 --play sample_05 1 --play 待機ループ00 3
# 空格暂停/恢复，Esc 关闭
build/dev/bin/nekomote_preview --animation-diagnostic --asset /path/to/model.nkm.json \
  --origin 400 940 --play sample_05 0 --steps 14 --hidden --screenshot frame.png
build/dev/bin/nekomote_pose --animation-diagnostic --asset /path/to/model.nkm.json \
  --plays '[{"label":"待機ループ00","flags":3}]' --benchmark 2000 --output benchmark.json
```

参考复现见 tools/reference_capture/README.md。截图入口在指定作者步骤冻结，实时入口按单调时间推进；Renderer 仍只处理源无关顶点/命令协议。GeometryScene 提供最大容量，预览在加载时 reserve，变形/资源切换时不扩容 GPU 缓冲。联合 PlaybackInstance 已集成物理与自动眨眼；时间线权重淡入淡出、反应和音频仍待实现。

发布态 CPU 作者步进+参数绑定+几何求值+构帧基线见下方独立记录；不包含 GPU、swap、音频/物理或桌面，不声称完整方案 FPS/功耗收益。

基线（2026-10-04，Release，Ryzen 7 5800H / NVIDIA RTX 3060 Laptop / 驱动 575.64.03 / KDE X11，同 P4 环境）：chocola-date-a，待機ループ00 difference，431 节点/33 sprite，120 步预热、2000 步采样，P50/P95/P99 = 0.530580/0.570250/0.714820 ms。每样本推进一个作者帧，变姿态，无截图、无 GPU 读回。日志 build/evidence/p5-animation-cpu-benchmark.log；可用上述命令复现。该数值与旧固定姿态基线的输入不同，不能直接宣称提速。


## Selector 子步骤

选择值在下一作者步向零截断为整数，选项权重独立过渡。初态第一个选项权重为 1，其他为 0；每作者步写入选项参数，因此单独写 fade 参数会在下次作者步被覆盖。改选或重复触发从保存权重继续，以完整权重区间的速度过渡；不能按剩余值差重新插值。控制器仅接受当前素材已经验证的 on=0/off=1 与 Linear 输出，重复/共享/循环输出明确拒绝；差分 Selector 尚未验收，继续拒绝。

数值轨迹包括延迟零时长、十帧过渡、分数/越界、中途改选/重复、平方缓动；fixture 不包含模型纹理或几何。原版物理启用的 Chocola/Fraise 半程淡入淡出图片、UV/顶点/拓扑通过；Chocola 跑步 12 步的参数/几何通过但 (186,365) 一个蓝色 3/255 像素 FAIL。九组控制器与运行时生命周期回归、完整失败和复现矩阵见 docs/validation/p5-selector.json、playback-selector-inputs.json。完整播放器未认证。

## Eye 跨区间子步骤

普通区间离开时先到 end_frame 闭眼连接点；返回时走最近连接点，等距选择闭眼点。十二组 288 个源数值通过。十组原版完整矩阵九组图片/几何/标量通过，Fraise eye=34 激活嵌套 motion.timeOffset=40 后仍明确失败，不计完整播放通过；字段路径和保留日志见 [Eye 验证](validation/p5-eye-routing.json)。自动眨眼调度尚未集成。


## 自动眨眼与嵌套瞳孔子步骤

眨眼使用每实例四字 UInt32 随机状态和独立 Float32 阶段缓存；按共同作者时钟推进，手动特殊眼形期间调度继续。普通区间投影为 `base+(end-base)*(blink/end)`；手动基础通道与眨眼值分开。随机间隔从低 23 位计算后保存 Float32；关闭/保持/打开边界在同一作者步继续，不能改成固定阶段延迟。核心没有墙钟或全局随机状态；`AnimationConfig.random_seed` 是显式输入，默认 0，reset 重放种子。

23 组 3480 个源标量样本验证随机种子、时长、固定/随机间隔、开闭范围、隐藏状态与手动过渡。完整原 PSB 图片包含多个作者步和 Fraise 闭眼/待机；最新结果见 [眨眼证据](validation/p5-automatic-blink.json)，严格图片 FAIL 保留，不认证整个引擎。30/60/144 Hz、暂停恢复、独立实例和 reset 回归覆盖同一作者时钟，稳态 500 次过渡/眨眼/几何构帧普通 C++ new 为零。

Fraise 左右瞳嵌套 `motion.timeOffset=20/40` 引用的根层均已参数化。原 PSB 与偏移归零、缩短自由 helper 终点的受控对照协议/PNG 完全相同，说明参数根层拥有采样时间；仅这一条件下偏移不覆盖参数时间。未参数化自由动作的非零偏移继续明确拒绝。特殊眼形的 blank 遮罩源无图元，原 GPU 清空遮罩而不绘制源；原生保持解析成功的空覆盖，未解析/嵌套遮罩仍拒绝。此前 Eye 矩阵的 Fraise point34 失败现已闭合，历史记录保留。


## 差分时间线权重与停止淡出

`set_timeline_weight` 接受有限任意权重、作者帧时长、缓动与具名完成策略 KeepPlaying/Stop；权重缓存延迟到下一作者步，零时长也不提前改变公开缓存。主播放模式的权重/淡出请求保持不变；差分模式单独过渡，最后将轨道值乘权重以 double 累加后保存 Float32。普通 stop 冻结比率并移除贡献；重播重置权重为 1。fade_out 在完成当步停止；fade_in 可启动已停止的时间线并从零重新推进，已播放差分从当前比率继续。默认 play 不自动淡入。

十九组公开 SDK 比率/播放状态轨迹已冻结，包括负/超过1、零时长、正反缓动、中途改向、重播、停止后设置权重和再次淡入。固定步进诊断工具可以重放 timeline_requests/events；JSON/字符串处理不进入实时作者循环。最终画面矩阵与剩余失败见 [权重证据](validation/p5-timeline-weights.json)。两条已支持差分的最终四个控制值与未截断 Float32 观察逐位相同；物理/画面仍有明确 FAIL。非线性控制器差分尚未验证，不能把任意时间线淡入当作全素材已支持。

混合差分动作已通过公开缓存与原版实际几何确定：`AdditiveParallel` 只将 Linear 轨道作为数值差分，Eye/Expression/Mouth/Immediate/Selector 仍派发到主控制器。权重和停止不会回退已经派发的主控制器过渡。此前 走行ループ 被拒绝的原因已修正；主表情、手势和待机可并行。详见 [控制器差分证据](validation/p5-controller-differences.json)。10 组原版会话所有几何与权重/播放查询通过，8 组完整标量/图像通过；半权重奔跑的物理精度及一次摸头反应的单像素仍未闭合。P5 保持进行中。
