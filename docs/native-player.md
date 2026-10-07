# 原生窗口播放器与设备服务

当前普通窗口入口是 `nekomote_player`。它使用本项目 runtime 求值和 GL 后端，加载独立模型及会话资产，支持自动眨眼、主/差分动画、物理、视线、触摸反应、真实声音/口型和 PNG 背景。2026-10-07第二轮整合已修复Fraise代表遮罩回归、背景时钟和部分宿主边界，115组冻结代表矩阵通过；未验证的局部继承及非标准混合仍明确拒绝。当前并非完整认证产物，详见 [接续审查](review-followup-2026-10-07.md)、[已知限制](known-defects.md) 和 [协作任务](agent-workboard.md)。

## 构建与素材

需 OpenGL 3.3、GLFW 的显示依赖、pkg-config 和 libpulse 17.x 开发包。PulseAudio 协议服务可以由 PulseAudio 或 PipeWire 提供；不修改桌面或声音服务配置。没有可用音频服务时明确失败，可显式 `--mute` 运行。

```sh
cmake --preset release
cmake --build --preset release
build/release/bin/nekomote_import \
  ../NekoWebShow/model_conversion/chocola/chocola-date-a/chocola-date-a.json \
  ../NekoWebShow/model_conversion/chocola/chocola-date-a/chocola-date-a.resx.json \
  --output assets/characters/chocola-date-a
python3 tools/import_session/import_session.py --reference-root ../NekoWebShow \
  --model chocola-date-a --output assets/characters/chocola-date-a/session.nks.json
build/release/bin/nekomote_player --model assets/characters/chocola-date-a/model.nkm.json \
  --session assets/characters/chocola-date-a/session.nks.json --fit \
  --background ../NekoWebShow/background/BG_07A.png
```

正式运行只需要传入的资产/背景和设备服务，不依赖源仓库、Python、Node、浏览器或 asm 核心。模型格式只接受 schema 5；会话格式只接受 version 1，不自动回退旧格式。

## 操作

- 鼠标：窗口内视线。左键：按作者区域/优先级触摸。右键拖动：移动角色。滚轮：以指针位置缩放。
- F：完整模型适配窗口；`--fit` 在启动时适配。R：重置会话。空格：暂停/继续。Esc：关闭。
- 默认显示 30 FPS；`--fps 60` 可选择 60 FPS（接受5–240）。作者时钟始终60 Hz，帧率只限制绘制。
- 窗口最小化或没有可用输出尺寸时暂停并停止绘制，恢复不追赶历史时间。窗口缩放保留用户模型位置与比例，F 可重新适配。
- `--mute` 关闭声音和声音口型；`--reaction chocola/h01` 可选择已导入反应。触摸忙状态不会重入同一反应。
- `--scale`、`--origin-x/--origin-y` 为中心原点坐标（Y 向下）；`--width/--height` 设置初始窗口尺寸。输入按实际 framebuffer 比例换算，核心不查询全局指针。
- PNG 背景采用居中 cover。后端显式 `TargetContents::Preserve` 在宿主已有内容上合成；角色独立中间目标每帧仍清空。

## 生命周期与声音

`asset_io::load_wave` 严格解码 RIFF/WAVE 的 8/16/24/32 位整数 PCM 或 float32，mono/stereo，拒绝压缩/畸形文件和非有限样本。加载时缓存所有声音和 512 帧 mono RMS 窗口；播放时不读文件。口型使用实际 PCM 的 RMS 输入，512 帧块近似设备位置，未声称与浏览器 analyser 的字节量化/采样窗口逐样本相同。

`audio::Device` 自己拥有 PulseAudio context/stream 和工作线程。生产者只写 32 项有界 SPSC 队列并读原子快照，`submit/snapshot` 不等待设备。播放位置由 `pa_stream_get_time` 的设备计时提供；未取得计时时 RMS 为零，不以墙钟假设已发声。暂停、停止、连接期间的控制及设备失败有明确状态；析构在宿主关闭时 join，GL 对象先在有效 context 下销毁。音频库公共头不暴露 Pulse 类型。

## 验证与证据（2026-10-04）

- Debug/Release 各 119 项，ASan/UBSan 118 项无图形测试通过。WAVE 签名、大小、padding、整数符号、float NaN、双声道 downmix 与 RMS 都有回归。
- GPU 回归验证 PremultipliedOver 与 ProtectedStraight 对已有背景的保留和显式 Clear。已有模型图像/遮罩测试继续通过。
- 实际 Chocola h01 设备验证：暂停后创建流、继续、停止、重播、无效 clip 拒绝；观察到真实 RMS 最大 0.416465，暂停/停止快照为零。
- 播放器 36 步 h01 与独立会话 probe 像素完全一致。原 WebGL 比较仍为已有 1 个失败像素（黑合成蓝色差 3/255），alpha 最大 1/255；阈值没有调整。并列图已经目视检查。
- 真实窗口按进程 ID 验证：800×1080 暂停截图两次像素完全相同，resize 到 640×800、F、触摸、reset 和退出无错误。第一次仅按标题的自动化误选了同名文件管理器窗口，恢复原尺寸后作废该组证据，未计入验收。
- 原背景、完整模型适配与触摸窗口截图已检查。所有产物位于 `build/evidence/p6-application/`；素材未提交进源码。

## 性能范围

本次 Release+LTO，Ryzen 5800H、RTX 3060 Laptop/NVIDIA 575.64.03、KDE/X11、800×1080，h01 + 待机差分，真实声音和 BG_07A；120 显示帧预热后 300 帧。CPU advance+prepare+GL 提交 P50/P95/P99 = 0.267056/1.045319/1.099373 ms；swap-call cadence 164.839 fps，P50/P95/P99 = 6.054440/6.081817/6.094108 ms。含加载的进程峰值 RSS 289,532 KiB，用户/系统 CPU 0.424576/0.318432 s，总墙钟 3.203371 s。该范围是 CPU 提交和 swap 调用节奏，没有测量真实呈现时间戳、GPU 帧时/显存或功耗，不能据此宣布优于网页或达到壁纸资源目标。

```sh
# 无截图、真实时钟、vsync；测量结束后自动退出
build/release/bin/nekomote_player --model /path/model.nkm.json \
  --session /path/session.nks.json --fit --fps 30 --measure-frames 300
# 独立固定作者步截图入口，必须显式静音，不计入性能采样
build/release/bin/nekomote_player --model /path/model.nkm.json \
  --session /path/session.nks.json --mute --transparent --seed 1 \
  --reaction chocola/h01 --capture frame.png --steps 36
```

截图仍由独立求值结果产生，没有使用参考引擎顶点或帧观察替代 runtime。Plasma 适配、全素材/长期稳定性、性能冻结及已记录几何/物理精度继续按 ROADMAP 推进。

## 显示节流与安全窗口自检（2026-10-05）

新默认30FPS，60FPS可选。相同硬件/输出/背景/声音/反应、120帧预热后300帧的最终清理版测量：30FPS实际29.807，CPU提交 P50/P95/P99 1.211471/1.593922/1.743453ms；60FPS实际59.360，1.044061/1.452983/1.543428ms。节奏包含swap和事件等待；没有实际呈现时间戳、GPU计时或功耗结论。首测约1FPS并被中止的条件尚未解释，原始失败保留，诊断及最终复测均正常，不将该异常宣称已修复。

固定36作者步在30/60FPS参数下逐像素相同；h01与原WebGL在alpha及黑白背景合成上误差为0（PNG直通RGB量化最多1/255）。全局按键测试曾影响Codex终端，已作废；从此不使用系统键鼠注入或标题选择窗口。

```sh
# 诊断：直接调用此应用绑定的回调，控制自有窗口，不发送系统按键/鼠标事件
build/dev/bin/nekomote_player --model /path/model.nkm.json \
  --session /path/session.nks.json --mute --fit \
  --check-window build/evidence/owned-window-check
```

该入口不抢占初始焦点，等待六次初始swap后独立保存暂停/resize/恢复截图，验证暂停像素相等、异步resize/fit、minimize/restore、reset/resume和退出。截图读回与分配仅发生在显式诊断模式；与性能测量互斥。证据见 docs/validation/p6-player-pacing.json。

Debug/Release 的窗口自检均通过。Release 曾在EGL初始drawable分配前取到仅1×1有效内容的800×1080截图；失败材料保留，自检先预热实际surface再比较暂停像素。

2026-10-05 刚性四角修复后，旧隔离动画7/7、完整代表模型矩阵均通过。上述2026-10-04失败是历史记录；所有原始材料保留。当前播放器固定h01及自有窗口检查均回归通过。

## 目录模式与当前限制（2026-10-07）

```sh
build/dev/bin/nekomote_player --assets assets --character chocola-date-a --mute --fit --no-save
```

目录模式扫描characters、backgrounds和icons，提供角色/UI选择、背景和角色过渡。`--no-save`用于诊断，避免写持久配置；配置目录内已有资产不等于型号通过认证。UI需Fontconfig和可用字体，当前CMake可选探测与代码必需依赖不一致，按G6修复。C/GLFW ABI宿主是实验性调用样例，有独立音频/错误/所有权缺口，不能代替正式桌面适配器。

R1的Fraise待机400步当前165超差像素，R2的Coconut body_LR30当前8.876px误差；文中早期通过和失败材料保留为当时记录。背景暂停/reset、切换失败和新UI状态尚需专门回归；原核心零分配与声音证据不覆盖新增UI及C宿主设备实现。

上段为首次审查时的记录；第二轮整合后的状态以上文及接续审查为准。

## 启动报错与型号支持（2026-10-07）

`--assets assets` 优先选用 `assets/config.json` 保存的角色。资产导入成功、目录存在或以前能出画面，都不代表该型号已通过WebGL一致性验证。

`session failure 6/3` 表示 `SessionError::Playback` / `PlaybackError::Geometry`，不是段错误。上一轮Codex提交 `a858f01` 恢复了对未验证继承、非零 `bm` 与非标准 `attr` 的严格拒绝；因此过去采用近似实现能显示的部分型号现在会拒绝加载。当前实例为保存的 `milk-summer`：活动节点 `■胸揺れ` 的源帧带 `bm=19`，报 `UnsupportedTransform`。Gemini V3交付了源GPU观测，原生实现及逐像素验收仍待完成。

播放器现在在初始化声音及GPU模型资源之前验证首帧，错误包含角色名、几何错误名称与节点路径。它不会自动把指定角色替换成其他角色，也不会放行近似效果。默认选中不支持型号时仍退出，恢复界面及目录能力提示待后续实现；改进诊断不等于修复该型号的渲染。

可以显式运行当前已验证的代表型号，诊断时保留原配置：

```sh
build/release/bin/nekomote_player --assets assets --character chocola-date-a --no-save
# 没有音频服务时额外加 --mute
```
