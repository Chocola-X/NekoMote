# 真实 Wayland 桌面证据

范围与结论见[报告](../../wayland-desktop-cpu-2026-11-10.md)。基线`ef47367`，
本轮提交中的代码为最终候选；二进制独立哈希见`hashes.json`。
候选桌面计数采样早于最后的span/连接生命周期收尾，不把两版哈希混用。

| 文件 | 含义 |
| --- | --- |
| baseline-counters.log | 本地插桩、输入限帧修复之前，真实60 FPS配置与自然鼠标活动 |
| candidate-counters.log | 输入限帧修复后，同一真实桌面的计数，包括后端切换/加载；启动区间不能当稳态 |
| final-minimal-*.log | 最终无模型FBO工具，在真实180 Hz输出分别运行basic、threaded |
| clean-*.summary.json | 本地输入修复候选、开启计数诊断，真实Plasma的25秒CPU/内存/GPU摘要 |
| real-layer.summary.json | 原Actions layer-shell、原模型/背景/配置、OpenGL、plasmashell退出时的25秒摘要 |
| initial-gl-profile.txt | 原Actions/OpenGL进程的20秒CPU采样，199 Hz、523样本；包含子调用比例不能相加 |
| initial-gl-inclusive.txt | 同一采样的children/self汇总，支持59.08%/31.74%/24.28%的调用树比例 |
| final-pacing-*.log | 最终共享item的GL/Vulkan隔离帧调度、像素与生命周期检查，附输入压力子例 |
| input-pressure-real.log | 真实basic输出、30 FPS与约7000输入/秒的专项检查；早于span收尾 |
| input-pressure-real-final.log | 最终span/连接收尾后的真实basic回调压力复查，约6840输入/秒、31 FPS |
| layers-*.log | 最终菜单/背景/模型输入fixture，两后端通过，120 FPS点击改为当前布局行中心 |
| item-safety.log | 未配置透明输出、正常播放、失败后停止输入与清理 |

大体积perf原始记录、JSONL计数、构建日志、临时截图和安装备份保留在被忽略的
`experiments/wayland-desktop-2026-11-10/`，不把用户配置、素材或整份驱动采样纳入版本。
CPU摘要的分位数是1秒CPU占用采样的分位数，不是帧时间分位数；RSS/显存不能直接
跨宿主归因为模型资源增加。GPU数值来自整张设备。

## 复现

```bash
cmake --preset plasma -DNEKOMOTE_BUILD_TESTS=ON -DNEKOMOTE_BUILD_PLASMA_PROBE=OFF
cmake --build build/plasma --target nekomote_plasma_frame_pacing nekomote_qt_fbo_probe

# 同一可见高刷输出顺序执行，无模型、无系统输入。
QT_QPA_PLATFORM=wayland QSG_RENDER_LOOP=basic build/plasma/bin/nekomote_qt_fbo_probe
QT_QPA_PLATFORM=wayland QSG_RENDER_LOOP=threaded build/plasma/bin/nekomote_qt_fbo_probe

# 全套像素/重建检查请使用可见、尺寸充足的隔离输出及threaded循环。
QT_QPA_PLATFORM=wayland QSG_RENDER_LOOP=threaded \
  ctest --test-dir build/plasma -R '^plasma_frame_pacing$' --output-on-failure
QT_QPA_PLATFORM=wayland QSG_RENDER_LOOP=threaded NEKOMOTE_TEST_BACKEND=vulkan \
  ctest --test-dir build/plasma -R '^plasma_frame_pacing$' --output-on-failure

# 实际basic循环只运行高频回调子例，避免混入既有重建等待问题。
QT_QPA_PLATFORM=wayland QSG_RENDER_LOOP=basic NEKOMOTE_INPUT_AUDIT_ONLY=1 \
  ctest --test-dir build/plasma -R '^plasma_frame_pacing$' --output-on-failure
```

本地未具备完整KDE开发包，因此`PLASMA_PROBE=OFF`；两个已有输入fixture另外使用
Qt6的pkg-config、仓库头文件、最终共享库和`-std=c++20 -fPIC -O2 -Wall -Wextra -Werror`
直接编译，私有KWin/threaded中运行。不是远端Actions/Qt6.4或Qt5.12基线认证。
Qt5.15.17插件与最小工具另以现有`build/plasma5`配置构建通过。

可在**独立启动的临时宿主**上用`NEKOMOTE_QT_PROFILE=1`收集5秒窗口的draw/window/input
计数；不要为排查修改整个用户会话的全局环境，也不要将开启诊断的CPU与关闭诊断的版本
当作严格配对收益。真实桌面重启及部署必须按实际授权执行。
