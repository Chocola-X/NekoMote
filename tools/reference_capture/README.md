# 受控原 WebGL 采样

这是开发诊断工具，不是正式播放器依赖。只读使用外部参考项目，不复制或改写其驱动。浏览器和 Node.js 路径由开发者显式提供。playwright 1.56.1 及依赖由 package-lock.json 固定。

```bash
npm ci --prefix tools/reference_capture
python3 tools/reference_capture/capture.py --reference-root ../NekoWebShow \
  --model chocola-date-a --browser /path/to/chrome \
  --output build/evidence/reference/default
# 可追加 --variables '{"head_LR":30}' --steps 60 --step-frames 1
# --trace 导出诊断几何/状态；--isolate-icon 0003 仅保留原驱动某图集区域的绘制
```

默认 800×1080、scale=1、coord=(0,400)、零固定步进。禁用 RAF，将 SDK hair/parts/bust scale 设为 0 并关闭风，绕过应用反应规则，以全新 player 采样。注意 scale=0 并不停止控制器积分；零步静态基线有效，带步进的物理隔离必须使用下述显式 fixture。保留原驱动渲染纹理及 WebGL 合成方式，包括透明颜色保护的两遍绘制。截图空白、页面错误或 GL 错误会失败；元数据记录源码 hash、默认变量、输入、GPU 和可见像素数。

隔离图集区域只用于基础 UV/alpha 对照；其过滤依据是诊断观测，不认证场景图或网格求值。trace 是验证数据，禁止作为正式原生运行时的顶点/动画数据来源。完整模型验证必须使用未过滤画面。

P3 支持两项额外诊断：`--trace-math` 记录原引擎的 sqrt/sin/cos/tan/atan/atan2/asin/acos/pow 调用；只在显式启用时替换 Math 入口，不能用于性能测量。`--disable-clamp` 在内存生成 PSB v4 测试 fixture，仅把 `/metadata/clampControl/*/enabled` 从 1 改成 0，记录每个字节、原/新 PSB hash。原文件保持只读，此类截图不是未经修改的原模型验收结果。默认命令仍使用原 PSB；最终控制规则必须通过原 PSB 的对照。

--fixture-icon-size tex/0077=213,213 只在内存修改所选 atlas icon 的 float32 宽/高，保持地址表和未选载荷不变；记录修改字段、字节范围与 hash。仅用于网格尺寸边界实验，不能替代原 PSB 的正式验收。外部目录始终只读。

`--trace` 另存 `.state.json`：目标附件、颜色位数、纹理类型/尺寸、blend、scissor、viewport 与 clear。`--trace-mask-pixels` 需同时启用 trace，额外导出 mask green 的直方图与非零像素；是显式 GPU 读回诊断，不用于性能或原生帧输入。

完整静态图像、实际原生几何与拓扑的 30 组验证入口：

```sh
python3 tools/reference_capture/validate_static.py --reference-root ../NekoWebShow \
  --importer build/dev/bin/nekomote_import --preview build/dev/bin/nekomote_preview \
  --evaluator build/dev/bin/nekomote_pose --browser /path/to/chrome \
  --output build/evidence/static-validation
```

输出目录必须新建。默认重新导入四型号并采样原 PSB；也可显式指定 `--reuse-reference <既有采样目录>`，此时核对原始素材、驱动、资产与 PNG hash，以及初态、变量和物理配置。工具不覆盖证据，逐组记录全部结果，有任一失败则最终返回非零。当前有已知 FAIL，见 `docs/validation/p4-static-images.json`；阶段/完整播放认证始终保持 false。

P5 时间线诊断支持 `--timeline LABEL`（可重复，按指定顺序播放）、`--timeline-flags N`、`--sample-variables` 和 `--observe-variable LABEL`。逐步记录原始变量与播放状态；额外 observe 用于 CountVariables 未列出的物理绑定。`timelineInfo.reportedLoop/reportedFrames` 仅保留 SDK 查询原值，目前它们与实际播放终点不一致，不能作为时长/循环真值。

`--transitions '{"body_UD":{"value":30,"frames":14,"easing":2.5}}'` 调用原 SDK 的变量过渡入口。`--disable-physics --disable-auto-blink` 在内存 PSB fixture 把相应控制器 enabled/blinkEnabled 从 1 改成 0，每个修改字节及所有共享该标量的字段都有记录。它们用于分别验证时间线与控制器，不能宣称为完整原模型动画验收。源 PSB 始终只读。步进大小和初态必须一致：原 SDK 的半帧步进与整帧步进存在事件/过渡顺序差异。

`--play LABEL FLAGS` 可重复，用于同次采样中逐动作指定标志（与 --timeline 互斥）。资产 diff 标签不替代播放标志：原 SDK 的 1=parallel、2=difference，所以典型主动作用 1、差分用 3。普通 GetVariable 不包含差分；必须以实际几何/画面对照。

该模型的 `blinkEnabled` 和 Eye `enabled` 共享标量地址，因此旧 `--disable-auto-blink` fixture 同时关闭 Eye 控制器，导致立即赋值。不能将其解释为眨眼开关改变过渡。涉及 Eye 轨道时使用仅 `--disable-physics`，或下述显式字段引用实验，独立保留 Eye enabled=1。旧数值和 hash 不重写，实际影响范围见 `docs/validation/p6-fixture-alias-audit.json`。

动画截图矩阵：

```sh
python3 tools/reference_capture/validate_animation.py --reference-root ../NekoWebShow \
  --importer build/dev/bin/nekomote_import --preview build/dev/bin/nekomote_preview \
  --evaluator build/dev/bin/nekomote_pose --browser /path/to/chrome \
  --output build/evidence/animation-validation
```

同静态入口要求新目录，可显式复用校验过的参考目录。冻结原 PSB/fixture/驱动/当前资产、截图和 trace hash，验收保留 0.001px 几何与 2/255 图像阈值；任何 FAIL 返回非零，完整播放认证始终 false。

P6 支持 `--physics-scales '{"hair":1,"parts":1,"bust":1}'`，默认仍为全零；这不是 enabled 开关。`--events '[{"step":2,"forces":{"bust":[1,0]}},{"step":3,"forces":{"bust":[0,0]}}]'` 在对应 Update 之前执行显式 SDK 外力请求；force 标签是 `bust`/`hair`/`parts`，不是控制器的日文名称。也可使用 `variables` 在指定步骤立即写变量。逐步 Math trace 包含作者步号。

`--fixture-numbers '{"/metadata/bustControl/0/spring":0.5}'` 和 `--fixture-strings '{"/metadata/bustControl/0/baseLayer":"center_fronthair"}'` 仅在内存改写同长度的 PSB 数字/现有字符串引用。限于明确 metadata/object JSON Pointer，拒绝插入字段、扩大数值宽度或不存在的字符串；地址表和资源保持原样，每次记录字段、字节范围、值和 hash。仅用于隔离语义，不能作为未经修改的完整模型验收。禁止把采样结果或此类修改加入正式播放路径。

`--trace-float-stores --trace-float-stack <已观察到的调用者名称>` 可显式记录完整 Float32 堆视图的数值写入前/后和调用栈，默认最多 5000 条，可用 `--trace-float-limit 1..100000` 显式调整，`--trace-float-step N` 只观察指定作者步；达到上限后 `floatStoresTruncated=true`。通过原生 typed-array 执行实际存储，方法绑定原视图，WebGL 的 subarray 仍为原生视图；不读取核心源码。只有诊断入口代理构造器，默认不改变 Float32Array。用于区分临时 double 和保存 Float32 的舍入顺序；必须对照未启用观测的同输入参数/截图确认无扰动，不能用于性能或作为原生帧输入。调用者过滤需显式指定，未指定时采集全部符合条件的写入。

PSB 字典可能共享标量存储，修改一个路径可能同时改变其它字段。每条 fixture 现记录 `affected_paths`（所有实际别名路径）和 `stored_after`（编码后的实际数值）；同一字节有冲突请求时拒绝，不能让后一次写入静默覆盖。分析或建立数值 fixture 时必须应用完整 affected_paths，而不是只使用 requested path。旧采样需要按原 PSB 地址审计，不能由单字段描述推断其隔离范围。

`--fixture-references '{"/metadata/eyeControl/0/blinkEnabled":"/metadata/eyeControl/0/beginFrame"}'` 将所选字典条目重指向同一字典中的既有数值标量，保持引用宽度，不插入资源或字段。此例使 blinkEnabled 引用已有的整数 0，而 enabled 仍为 1，允许独立核实眨眼与 Eye 过渡。它是显式地址表修改，记录 kind=field_reference、来源路径、原/新地址和改变的条目；不等同普通数值 fixture 的地址表不变承诺，不用于正式原模型验收。

工具自身的无素材回归：`python3 -m unittest discover -s tests/reference_capture -v`。


联合会话固定步进对照见 [P6 会话记录](../../docs/p6-playback-session.md)。validate_playback.py 使用独立冻结输入、原始素材与正式 PlaybackInstance/preview，验证 22 个实际物理输出、最终 RenderFrame 和截图；原 SDK GetVariable 的主通道缓存不能代表差分合成结果。几何工具 compare_playback_frame.py 只服务当前单图集 SDK texture 2 的冻结诊断，不是运行时兼容分支。已知完整跳跃/待机差分失败仍保留，命令退出 1。
