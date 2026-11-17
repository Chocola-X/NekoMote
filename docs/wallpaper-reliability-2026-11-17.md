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
