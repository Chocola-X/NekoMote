# 壁纸可靠性改进与验证（2026-11-17）

本轮范围按用户指定顺序：音频故障隔离与恢复、锁屏/关屏节能、独立壳热插拔恢复。
各阶段分别验证、提交；资产目录重新扫描和额外控制接口不在本轮范围。

## 1. 音频故障隔离与恢复

基线 `7430f20`。原实现创建音频设备时在图形线程等待最多6秒；独立 player
会因连接失败退出，共享壁纸实例遇到运行中音频故障也会返回播放错误。
现在设备在音频工作线程按1、2、4、8、16、30秒退避重连，不等待图形线程，
失联时RMS为0。共享壁纸和player继续推进模型。队列满或失联时消费并丢弃
该次语音，不在恢复后补播；连接代次隔离失效前的命令，暂停状态独立保存。
静音仍由用户控制，不把服务失联写成持久静音配置。输入/PCM结构错误仍正常报错。

验证环境：本机 Linux/X11、E5-1680 v4、RTX3080Ti/NVIDIA580.82.09，
GCC Release `-O3 -DNDEBUG -flto`，PulseAudio16.1私有Unix socket + 8000Hz单声道
null sink。测试服务及其运行目录由工具独立拥有，不修改用户服务、输出设备或桌面配置。

- Release/LTO/Werror构建 player、音频测试、ABI边界测试通过。
- `audio_device_contract`、`abi_negative_test`、`abi_test`、`abi_synthetic_boundary`通过。
- `tools/test_audio_recovery.py`：已有播放、暂停时钟、自然结束、反复播放与销毁
  199项断言通过；新增首次无服务、运行中断线/重连、失联暂停、过期语音不补播、
  重试期间销毁不超过250ms，共127项断言通过。
- 缺失音频服务运行真实 Chocola `casual-b-with-bell`，OpenGL、800×600，预热120帧，
  采样180帧正常退出；CPU推进/准备/提交P50/P95/P99为1.254/1.537/1.665ms，
  实际59.668FPS，RMS0。此项证明动画继续播放，不作为前后优化收益比较。
- 独立设备10秒无服务探针（同编译选项）：旧版CPU0.762ms，新版1.815ms；
  新版创建0.093ms、销毁0.126ms。旧版不恢复，新版完成4次连接尝试，新增约1ms CPU/10秒。
  这只是设备故障路径测量，不包含加载、渲染、桌面合成及功耗。

复现私有服务测试（需PulseAudio程序和模块；可以使用隔离解包路径）：

```sh
python3 tools/test_audio_recovery.py --pulseaudio /usr/bin/pulseaudio \
  --modules /usr/lib/pulse-17.0/modules \
  --tests build/release/bin/nekomote_audio_device_tests
```

未覆盖：真实用户音频服务重启、蓝牙/USB音频设备切换、跨发行版/ARM64、长期与功耗。

## 2. 锁屏、休眠与关屏

基线 `c129e04`。新增仅宿主依赖的 `host_session`，通过系统 libdbus≥1.12
被动读取 ScreenSaver Active、logind LockedHint / PrepareForSleep。查询禁止自动启动
服务；无能力时记录原因，继续基础播放；不使用 ext-session-lock 请求锁屏，不把
用户闲置当成关屏。通知按宿主1Hz处理，不在模型核心引入桌面策略。
X11还通过已有Xext读取DPMS状态。用户暂停、宿主隐藏、锁屏/休眠分别合并，
宿主恢复不会覆盖用户暂停；切换状态时以 `advance(0)` 刷新音频命令并重置宿主时钟。
Plasma5/6共享通知逻辑，暂停时关闭帧定时器。

新增系统依赖仅在X11、layer-shell或Plasma启用时发现；核心和普通播放器不需要
libdbus。三份基线镜像、Arch包与CI必选测试构建目标已同步。

同机Release/LTO，真实Chocola `casual-b-with-bell`、800×600、面板60FPS。
使用自建KWin虚拟双输出、XWayland及私有D-Bus，合成ScreenSaver通知；每组预热2秒，
每次状态切换后等待1.5秒，采样3秒。CPU来自 `/proc/<pid>/stat`，分辨率0.01秒；
采样期间没有Wayland跟踪、截图或profiler。

| 壳/后端 | 旧版锁屏CPU秒/3秒 | 新版锁屏CPU秒/3秒 | 新版恢复后CPU秒/3秒 |
| --- | ---: | ---: | ---: |
| layer-shell / OpenGL | 0.21 | <0.01 | 0.33 |
| layer-shell / Vulkan | 0.18 | <0.01 | 0.19 |
| X11 / OpenGL | 0.17 | <0.01 | 0.16 |
| X11 / Vulkan | 0.19 | <0.01 | 0.22 |

[原始采样](validation/wallpaper-reliability-2026-11-17/session-cpu.json)也保留正常播放值；
短采样波动明显，不据此宣称正常播放更快或完整方案功耗下降。这里确认的是
原来持续渲染的锁屏状态现在停止推进/提交。

- 私有D-Bus契约通过：首次已锁定的状态读取、锁定/解锁、logind属性失效重新查询、
  休眠与锁定合并、服务消失及重新注册。CI无桌面可执行，不需要真实音频或模型资产。
- Plasma OpenGL/Vulkan实建QQuickWindow + 真实资产通过：锁屏后渲染计数停止，解锁恢复，
  用户暂停和窗口隐藏不会被解锁覆盖。Plasma5/6模块Release/LTO/Werror构建通过。
- 252项CPU用例、54189项断言通过；6组相关宿主/音频/ABI契约通过。
- 关屏命令确实在私有KWin发送，但协议回复虚拟输出 `supported(0)`，模式仍为On，
  因此[记录](validation/wallpaper-reliability-2026-11-17/session-contracts.json)明确标记不支持。
  **物理关屏、原生Xorg的DPMS状态切换、真实登录锁屏器与系统休眠未验收**；
  不把模拟通知、窗口隐藏或虚拟输出命令当成这些实机操作已通过。

本机虚拟KWin/NVIDIA会输出已知的合成器FBO错误，本轮判据是客户端生命周期、
帧调度及CPU；没有据此认证最终合成像素。实体多屏、其他驱动/合成器、ARM64、
远端CI和长期功耗仍需独立验证。Wayland的屏幕关闭仍依赖合成器抑制帧回调，
没有通用的被动关屏通知时不凭空判定不可见。

隔离审计入口为 `tools/testing/run_reliability_test.py --help`，必须显式提供构建产物、
模型/会话及证据目录。它自建并回收KWin、D-Bus和测试服务，不连接用户桌面或注入输入。

额外回归：XWayland/desktop GL上的既有帧调度程序完整通过，包含暂停缓存、resize、
场景图重建、无限FPS、隐藏恢复及输入压力（7864次回调，实际29.99FPS/目标30）。
Qt/Wayland/GLES上的同一程序在“暂停后隐藏并释放场景图”环节等待新renderer超时；
重新构建基线 `c129e04` 同样复现。尝试仅在场景图初始化后请求一次update未解决，
已撤销；没有借本轮加入无收益补丁，也没有把该路径标为通过。
新增锁屏/解锁及隐藏恢复测试在Qt/Wayland/GLES的GL/Vulkan两后端均通过，
但不覆盖上述暂停中强制销毁的既有问题。结果见[Plasma回归](validation/wallpaper-reliability-2026-11-17/plasma-regression.json)。


## 3. 独立壳输出移除与恢复

基线 `a516f68`。X11和layer-shell现在在目标输出消失后暂停，等待原来同名输出；
启动时仍须有活动输出，不自动选择另一屏幕。默认输出在首次确定后也记住其名称。
用户暂停、面板配置、模型实例、物理状态和GPU纹理保留，恢复重置宿主时钟，
不补算断开期间的墙钟时间。等待期间按事件/最多1Hz处理，SIGTERM可正常退出。
这部分只修改宿主生命周期，不修改模型求值、绘制公式或配置schema。

X11隐藏原窗口，重连按名称重新查询RandR XID、位置及尺寸，不依赖旧XID。
layer-shell释放失效的layer/EGL窗口或Vulkan surface/交换链，重建同名输出上的
原生surface；OpenGL将原上下文停放为无surface上下文，Vulkan保留既有设备和
离屏目标。替换Vulkan surface显式验证原物理设备/队列支持，不重新选卡。
替换失败释放新surface并报告错误；CPU用例覆盖录制中拒绝、队列不支持/查询失败、
设备等待失败及新旧surface/交换链/信号量的所有权。

恢复要求输出名称稳定；layer-shell需要 `wl_output` v4，OpenGL还需要
EGL1.5或KHR无surface上下文能力。不支持时明确失败。非输出移除引起的layer关闭、
合成器/显示服务器断开、GPU设备丢失仍退出；本轮没有实现这些重连，也不承诺拔掉
实际渲染GPU后仍可使用原设备。等待期间保留模型和纹理，因此不宣称释放全部内存/显存。

同机Release/LTO、真实Chocola `casual-b-with-bell`、800×600、60FPS限制，
使用私有KWin双虚拟输出及其XWayland，通过私有 `kscreen-doctor` 禁用/启用输出：

- 两壳×GL/Vulkan×指定/默认输出，共8组，每组恢复两次。固定1/60步长、初态/随机种子
  一致，在第240帧读取客户端画面；与冻结基线正常播放的对照逐字节一致，SHA256均为
  `610c3ae9b37fa659aec81d82c99fd4533501b6496d5f26d9afff093834cb7003`。
  截图只用于正确性，不参与性能采样。最终独立复测还直接启动冻结基线，确认四条
  旧路径在输出移除后均退出；新版8组图像恢复和4组失联退出再次全部通过。
  [最终16项记录](validation/wallpaper-reliability-2026-11-17/hotplug-final.json)。
- Vulkan另开Khronos及同步验证，指定/默认输出恢复和失联退出均为0错误、0警告。
- 首次失联退出采样中，layer GL/Vulkan、X11 GL/Vulkan的等待CPU秒/3秒分别为
  0、0.02、0、0.01；`/proc`分辨率0.01秒，不解释为绝对零开销。
  等待中SIGTERM到正常退出约0.113秒。该测量不包含截图、协议跟踪或验证层。
- Release/LTO/Werror双壳构建和6组宿主/会话/音频/ABI契约通过。
  [既有layer-shell隔离OpenGL集成检查](validation/wallpaper-reliability-2026-11-17/layer-regression.json)
  通过，涵盖协议缺失、缩放、输入及退出等。
- 另行启用开发工具和双壳、关闭Vulkan的Debug构建通过。此组合发现并修复了新增
  会话监听测试探针与既有模型诊断工具的CMake名称冲突：新增目标改为
  `nekomote_host_session_probe`，原 `nekomote_session_probe` 模型工具保持原职责；
  CI构建目标、CTest及文档入口同步，并实际构建两个工具验证可共存。

协议输出禁用/重启能真实触发原生对象移除和新建，但**不等同物理线缆拔插**；
原生Xorg、不同合成器/驱动、输出名称变更、全部输出同时消失、实际GPU设备移除、
长期循环及内存/显存增长尚未覆盖。沿用第二阶段的虚拟合成器限制，不据客户端截图
认证最终屏幕合成像素。


### 正常播放开销复核

为避免把加载/退出和实际提交数混在一起，新增显式构建的诊断库
`nekomote_host_present_metrics`，仅以 `LD_PRELOAD` 注入被测客户端。
GLX/EGL交换和Vulkan accepted present使用同一边界，预热120次，采样随后480次；
采样路径只计数和读取单调时钟，进程CPU时钟只在区间两端读取，不读回像素，
最终退出才排序/写文件。它不链接正式壳、不安装、不在CI/正式运行默认启用。
本表计整个客户端进程的CPU时间，包括驱动线程；间隔是**宿主提交间隔，非物理呈现时间**，
不含合成器CPU。无验证层，每组新旧交错各3轮，表中为各指标中位数：

| 壳/后端 | CPU秒/480次：旧→新 | 提交FPS：旧→新 | P95间隔ms：旧→新 | P99间隔ms：旧→新 |
| --- | ---: | ---: | ---: | ---: |
| layer/opengl | 0.7171 → 0.7910 | 58.15 → 57.95 | 18.101 → 18.154 | 18.704 → 18.750 |
| layer/vulkan | 0.6260 → 0.6185 | 58.00 → 58.00 | 17.825 → 17.737 | 18.493 → 18.408 |
| x11/opengl | 0.5504 → 0.4487 | 58.70 → 58.92 | 17.787 → 17.788 | 18.258 → 17.989 |
| x11/vulkan | 0.4915 → 0.4926 | 58.00 → 58.08 | 17.790 → 17.795 | 18.360 → 18.358 |

第一批layer/OpenGL CPU中位数上升10.3%，不能直接接受为无回退。随后专门对该组合
交错复测各6轮，中位数为0.7671→0.6844秒（方向反转，约−10.8%）；
P95为18.057→18.027ms，P99为18.676→18.625ms，FPS为57.94→58.14。
两批合计各9轮的CPU取值范围明显重叠，尚不能将这些差值归因为代码的稳定收益/损失。
结论是提交节奏未见持续回退，新增恢复路径可保留；**不宣称正常播放性能提升**。
初次包含启动/退出的360帧结果也保留，避免只报告有利样本。
未测整桌面CPU、功耗、RSS/显存趋势或物理呈现，不能由这些短样本外推长期节能。

可复现入口（真实资产与图形环境可用时）：

```sh
cmake --build build/release --target nekomote_host_present_metrics
python3 tools/testing/run_reliability_test.py \
  --layer build/release/bin/nekomote_layer_shell --x11 build/release/bin/nekomote_x11 \
  --probe build/release/bin/nekomote_host_session_probe \
  --model /path/to/model.nkm --session /path/to/session.nks.json \
  --before-layer /path/to/frozen-layer --before-x11 /path/to/frozen-x11 \
  --skip-native --frame-benchmark \
  --present-metrics build/release/lib/libnekomote_host_present_metrics.so \
  --evidence-dir /path/to/new-evidence-directory
```

使用 `--hotplug` 替换性能参数可执行输出移除恢复/固定帧画面对照；
`--benchmark-shells layer --backends opengl --benchmark-rounds 6` 可进行针对性复测。
工具依赖本机KWin、XWayland、D-Bus与kscreen-doctor，自建隔离会话，不能用于认证其他合成器。

原始记录：[输出恢复](validation/wallpaper-reliability-2026-11-17/hotplug.json)、
[Vulkan验证](validation/wallpaper-reliability-2026-11-17/hotplug-validation.json)、
[稳态三轮](validation/wallpaper-reliability-2026-11-17/steady-frames.json)、
[GL六轮复测](validation/wallpaper-reliability-2026-11-17/steady-layer-gl-repeat.json)、
[含启动的初次比较](validation/wallpaper-reliability-2026-11-17/frames-with-startup.json)、
[环境与二进制/输入hash](validation/wallpaper-reliability-2026-11-17/hotplug-environment.json)。
