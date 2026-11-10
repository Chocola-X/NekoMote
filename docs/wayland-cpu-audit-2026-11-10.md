# Plasma 帧调度与 Wayland CPU 复核

后续已在用户授权的真实180 Hz Wayland桌面复核：
[输入限帧修复、Qt basic重复提交与已知缺陷](wayland-desktop-cpu-2026-11-10.md)。
下文虚拟60 Hz测量仍只代表其限定条件，不代表剩余桌面差距已经消除。

2026-11-10，基线 `263f3eb`（生产代码同 `f08b7f3`）。原建议见
[分析报告](wayland-cpu-analysis-2026-11-10.md)。本轮修复不限帧率的 GUI 忙轮询，
并对暂停后无变化的 FBO 复用已有内容；不能据此宣布真实 Plasma 的 Wayland/X11
CPU 差异已全部消除，也不能将剩余差异直接归为无法修复的架构问题。

## 建议逐项核对

| 报告建议或解释 | 实际代码与处置 |
| --- | --- |
| Wayland 180 Hz 必然让 FBO 的 advance/render 跑 180 次/秒 | 不成立。Qt 的 FBO 节点以 `renderPending` 控制是否调用用户 renderer；别的场景图内容重绘不自动触发它。保留有限帧率定时器。 |
| 不限帧率的 0ms 定时器存在忙轮询 | 成立。改为当前帧提交后通过 `frameSwapped` 排队请求下一帧；只在不限帧时连接，切回限帧时断开并恢复 timer。停止 timer 本身不会维持动画。 |
| GLES 3 不支持 NPOT，因此 POT 是平台必需开销 | 不成立。现有 POT 是经画面对照保留的采样精度约束，不能删除；199/202 图集的数字是全素材容量求和，不是同时驻留的显存或 CPU 实测。 |
| 强制 desktop GL 可以修复 CPU 问题 | 未证实，不实施。插件不能修改宿主全局 `QSurfaceFormat`；独立审计进程可指定上下文做对照。 |
| 无条件用 `!needs_render` 跳过 | 会冻结动画。它只表示 presentation dirty，正常 advance 不会每次置脏。与 X11/layer-shell 一致，仅实际暂停且无变化时跳过。 |
| 用 `QQuickWindow::setMinimumRenderInterval()` 限帧 | 项目 Qt 5.12/6.4 基线无此 API，不采用。 |

Qt 的门槛及更新入口可直接查
[Qt 5.12.8 源码](https://github.com/qt/qtdeclarative/blob/v5.12.8/src/quick/items/qquickframebufferobject.cpp)、
[Qt 6.4.2 源码](https://github.com/qt/qtdeclarative/blob/v6.4.2/src/quick/items/qquickframebufferobject.cpp)。
原 D1 的 17.7% 来自旧提交的一次 Wayland 桌面测量，没有同条件 X11 或实际 FBO FPS，
不能用于确认当前代码的三倍重绘或量化各因素占比。

## 实现边界

- 有限 FPS 保持原有间隔公式，包含 180/240；每次 timeout 读取 render 线程发布的限制。
  不限帧时停止 timer，由该窗口提交后的排队通知续帧。窗口切换、暂停、隐藏与失败沿用
  原生命周期判断，旧窗口排队通知检查来源。恢复通过显式 update 引导第一帧。
- 跳过发生在资源初始化、Vulkan 上一提交等待、输入和 advance/poll 之后，清屏与
  `begin_frame` 之前。保留异步完成和 UI dirty 的处理，避免销毁仍被 GPU 使用的资源。
- `createFramebufferObject` 显式置脏；只有成功绘制/提交后清除，覆盖缩放、同尺寸
  重建和 GL 名称复用。暂停启动不再因 `initialized_time == false` 每次重复 set_paused；
  每次模型重载重新同步一次时钟/暂停状态。
- `rendered_frames` 现在统计实际壁纸绘制，诊断帧通知仍保留。外层暂停本来就会停 timer，
  因此 FBO 复用的收益只属于额外 update 等场景，不是所有暂停场景的新收益。

## 验证方法与范围

使用项目内静态核心、Qt Quick 宿主与共享 Plasma item；没有安装插件或修改用户桌面。
私有 runtime/config/cache/data、私有 DBus 和 KWin 虚拟输出用于可呈现测试。X11 路径是
同一私有 KWin 下的 XWayland，不代表原生 Xorg/Plasma X11 登录会话。

环境：Xeon E5-1680 v4（8核16线程）、RTX 3080 Ti、NVIDIA 580.82.09、Linux
6.6.107、KWin 6.3.6、Qt 6.9.2；GCC 15.2.1，Release/O3/LTO/Werror。
Qt5 共享插件另以 Qt 5.15.17 编译；不是 Qt5.12/GCC10 基线或远端 Actions 认证。
虚拟输出 2560×2160、约60 Hz；item 640×480。隔离目录只链接 `chocola/date-a`，
无背景，静音，不保存配置，无系统输入。比较前冻结生产动态库与审计二进制。
测量使用种子0；固定画面检查使用种子1，暂停重载子例改为种子2。

新增 `nekomote_plasma_frame_pacing`：默认检查有限/不限帧切换、暂停/恢复、隐藏/恢复、
暂停 resize/scene-graph 重建/模型重载及旧帧保留；之后退出固定帧模式，比较模型区域
确认 0→60→0 和隐藏恢复后的动画确实变化。静态内容做逐像素比较，截图不进入性能采样。
缩小时图标会按原逻辑 clamp，放大后保留位置，故往返 resize 比较模型区；重建恢复默认
图标位置时再比较初始整图，不把原有布局行为当作缓存损坏。

测量模式通过 `NEKOMOTE_PACING_MEASURE=1` 启用，跳过候选专属断言及所有截图。
正常播放预热2秒后采样约2秒；暂停稳定250ms后以5ms定时器注入重复 item update 再采样；
每次 FPS 配置切换稳定500ms。不限帧两次采样，交替运行基线与候选。CPU 使用
`CLOCK_PROCESS_CPUTIME_ID / wall`，含 Qt/驱动线程，不含独立 KWin。帧间隔来自 GUI
收到的 `frameSwapped` 通知，不是物理扫描输出或 GPU 执行耗时。P99 样本少，单独记录波动。

最终数字及检查结果见[证据目录](validation/wayland-cpu-2026-11-10/README.md)。

| 主要工况（三轮交替） | 基线→最终进程 CPU，单核=100% | 壁纸绘制 FPS |
| --- | --- | --- |
| Wayland/GLES，不限帧 | 102.30% → 12.83%（-87.46%） | 60.02 → 59.96 |
| XWayland/desktop GL，不限帧 | 37.40% → 11.24%（-69.93%） | 59.88 → 60.05 |
| Wayland/GLES，60 FPS限制 | 16.27% → 15.09%（-7.21%） | 58.71 → 58.81 |
| XWayland/desktop GL，60 FPS限制 | 9.73% → 9.16%（-5.83%） | 58.95 → 58.86 |

不限帧的主要收益来自消除忙轮询，不能把87%推广到默认60 FPS。暂停且人为重复 update
时，最终壁纸绘制数为0，CPU分别下降36.20%/40.83%，Qt窗口仍会合成/提交。
Wayland不限帧交换通知的P99为18.23→18.67ms，局部增加0.44ms；结合CPU和帧率接受，
不将短样本尾部抖动说成全部改善。进程峰值RSS相近；稳态驻留、显存/GPU时间/功耗
没有独立测量。

Qt5/Qt6构建及五组共享item回归通过；额外的私有Vulkan同步验证通过。旧Qt诊断工具
14个测试方法中13个通过，生命周期方法的两个子例在冻结基线与最终版均SIGSEGV，
未计为通过。它仍需单独定位；详见证据目录。真实Plasma、原生Xorg、多屏、180Hz及长期
运行没有覆盖，60 FPS残余平台差异的来源仍需真实宿主配对采样。

## 复现入口

```bash
cmake --preset plasma -DNEKOMOTE_BUILD_TESTS=ON -DNEKOMOTE_BUILD_PLASMA_PROBE=OFF
cmake --build --preset plasma --target nekomote_plasma_frame_pacing nekomote_qt_quick_host

# 在可见、尺寸足够的隔离输出中运行，不向真实桌面注入输入。
NEKOMOTE_TEST_ASSETS_DIR="$PWD/assets" QT_SCALE_FACTOR=1 \
  ctest --test-dir build/plasma -R '^plasma_frame_pacing$' --output-on-failure

# 独立宿主的测量；分别运行冻结基线和候选，各轮交换运行顺序。
NEKOMOTE_TEST_ASSETS_DIR="$PWD/assets" QT_SCALE_FACTOR=1 QSG_RENDER_LOOP=threaded \
  QT_QPA_PLATFORM=wayland NEKOMOTE_TEST_GL_API=gles \
  NEKOMOTE_PACING_MEASURE=1 build/plasma/bin/nekomote_plasma_frame_pacing
```

`NEKOMOTE_TEST_BACKEND=vulkan` 选择应用 Vulkan 后端，Qt 场景图仍为 OpenGL。
`NEKOMOTE_TEST_GL_API=desktop|gles` 只影响独立测试程序，不进入插件设置或运行策略。
旧基线没有此 runner 时，可将同一测量源码作为外部 probe，使用基线头文件和动态库编译；
两版不能混用不同布局的 item 头文件，动态库选择通过独立目录和 `LD_LIBRARY_PATH` 固定。
