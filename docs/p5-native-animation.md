# P5 原生动画子步骤（进行中）

2026-10-04。已接入原生主时间线、已验证的线性表情路径、并行与待机差分、60 Hz 作者时钟及普通窗口。仍是显式诊断；完整动画/物理播放器未认证。P3/P4 既有精度和嵌套验收也未闭合。

## 数据和运行时契约

Schema 3 加入强类型 variable_controls，保留区间/连接点、眼/口型/选择器规则。运行时不解析 source.json，不根据变量名或 instantVariableList 猜测行为。四型号除新增控制数据外与 schema 2 model 对象逐字段相同，迁移证据见 docs/validation/p5-schema3-migration.json；必须重导入旧 bundle，无兼容读取。

AnimationProgram 编译控制图/轨道与 GeometryScene 的参数 ID，拥有全部只读数据；原资产与几何场景不必继续存活。AnimationInstance 借用 Program，后者须更晚销毁；独立实例不共享变量、播放游标或差分状态。所有操作同一求值线程。

帧路径复用全局基础通道、各差分轨道及输出数组。主动作停止/替换后，已经启动的变量过渡仍向原目标推进，停止只取消后续事件。差分在独立通道求值，每帧从基础值加一次，避免累计漂移。0=替换、1=并行主动作、3=并行差分；资产 diff 字段不代替显式播放模式。

关键帧 type=2 启动目标过渡，duration=next_time−time−1；type=0 终止轨道。只支持经验证的整数作者帧、终端与循环形态，未知类型、非整数时刻、选中的未验证控制器明确失败。主时间线与差分循环首帧的更新边界不同，差分在回绕帧不推进通道，下一帧恢复；真实 420/421/422/430 帧几何对照支持该规则。

表达式路径先减少区间跳转，再选择靠近目标的连接点，不能按全局数值距离最短路径拟合；跨区间在当前作者步跳转并推进新段，不保留跨界余量。线性路径已用 11 组正反向/不同起点 SDK 数值轨迹验证。非线性跨段 easing、selector fade、非零跨区间 Eye 路径仍明确拒绝，不把候选公式作为正式支持。

裸变量与已禁用的变量控制器在请求时直接赋值；启用控制器按自身规则覆盖。Mouth 的 talkLabel 是独立插值通道，不按裸变量处理。不能从 instantVariableList 推导整个模型的策略。Eye 的手动过渡与自动眨眼开关分开；自动眨眼调度仍未集成。

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

参考复现见 tools/reference_capture/README.md。截图入口在指定作者步骤冻结，实时入口按单调时间推进；Renderer 仍只处理源无关顶点/命令协议。GeometryScene 提供最大容量，预览在加载时 reserve，变形/资源切换时不扩容 GPU 缓冲。物理、自动眨眼、权重淡入淡出和反应/音频仍未集成。

发布态 CPU 作者步进+参数绑定+几何求值+构帧基线见下方独立记录；不包含 GPU、swap、音频/物理或桌面，不声称完整方案 FPS/功耗收益。

基线（2026-10-04，Release，Ryzen 7 5800H / NVIDIA RTX 3060 Laptop / 驱动 575.64.03 / KDE X11，同 P4 环境）：chocola-date-a，待機ループ00 difference，431 节点/33 sprite，120 步预热、2000 步采样，P50/P95/P99 = 0.530580/0.570250/0.714820 ms。每样本推进一个作者帧，变姿态，无截图、无 GPU 读回。日志 build/evidence/p5-animation-cpu-benchmark.log；可用上述命令复现。该数值与旧固定姿态基线的输入不同，不能直接宣称提速。
