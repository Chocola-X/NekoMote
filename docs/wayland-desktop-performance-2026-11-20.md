# Plasma Wayland 桌面性能实测（2026-11-20）

本次在同一个真实 Plasma 6 Wayland 登录会话中，对比静态壁纸、NekoMote Plasma 插件和独立 layer-shell 壳。两种 NekoMote 入口均测 OpenGL 和 Vulkan。

## 环境与方法

| 项目 | 条件 |
| --- | --- |
| 系统与桌面 | Manjaro Linux x86_64，Linux 6.6.107，Plasma / KWin 6.3.6，宿主 Qt 6.9.2 |
| 硬件 | Xeon E5-1680 v4（8 核 / 16 线程），RTX 3080 Ti，NVIDIA 580.82.09 |
| 显示 | DP-2 单屏，2560×1440 @ 180 Hz，缩放 1，HDR 关闭 |
| 负载 | `chocola/wintermaid-b`，静态背景 `BG_07A`，壁纸限帧 60 FPS，静音，控制面板关闭 |
| 构建 | 用户下载的 Debian 12 x86_64 Release CI 产物，插件构建用 Qt 6.4.2，已安装的原生库与下载包逐字节匹配，layer-shell 直接运行包中程序 |

每种动态场景采两轮，测试首尾各采一次静态基线。每轮重启 Plasma Shell，始终保留同一个 KWin 进程。静态基线和独立壳场景中，Plasma 使用 `org.kde.image` 显示同一张背景，运行进程的映射中没有 NekoMote 插件。采样在会话内的临时空白虚拟桌面进行，保持面板与桌面效果。每轮预热 20 秒（独立壳启动后先额外等待 3 秒），采样 40 秒。截图在资源采样之外进行。

使用 [进程与 NVML 采样器](../tools/perf_compare/plasma_desktop_measure.py) 每秒读取进程计数与 NVIDIA NVML。CPU 的 100% 代表一个逻辑 CPU，桌面 CPU 为 Plasma Shell、KWin 与 layer-shell（运行时）的和。进程 RSS 含共享页，整卡显存、利用率和板卡功耗也包含会话中其他程序。Plasma 插件启用了发布包自带的 `NEKOMOTE_QT_PROFILE=1`，每 5 秒记录壁纸绘制次数及窗口交换次数。

验证归档的 SHA-256 与下载校验文件一致。归档和执行文件的指纹如下。

| 文件 | SHA-256 |
| --- | --- |
| `nekomote-plasma6-debian12-x86_64.tar.gz` | `bde424b619609101652c59fb5daf4261d01eeb391ad07f70945f9f022fce4139` |
| 已安装的 `libnekomote_plasma_qml.so` | `b9143dc3f1e0c8d8de8dfe66d4906fcffd189efaba3891144a5550e2e1d280e7` |
| `nekomote-layer-shell-debian12-x86_64.tar.gz` | `e87a10cc2cb6738136e0991156e2153445f5d8cc8b3d2dc6dc24d8dc9007ae6e` |
| 执行的 `nekomote_layer_shell` | `b99875087b1984a2227dfe9eca6813e9909362263709f5335f685ea67e4bac2f` |

## 结果

下表为两轮均值，CPU 使用单逻辑 CPU 口径，增量以两轮静态基线的均值为准。

| 场景 | Plasma Shell CPU % | KWin CPU % | layer-shell CPU % | 桌面 CPU 合计 % | 相对静态增量（百分点） |
| --- | ---: | ---: | ---: | ---: | ---: |
| 静态图片 | 1.3 | 0.8 | — | 2.1 | — |
| Plasma OpenGL | 18.6 | 11.4 | — | 30.0 | +28.0 |
| Plasma Vulkan | 18.8 | 11.0 | — | 29.8 | +27.7 |
| layer-shell OpenGL | 1.1 | 6.7 | 12.8 | 20.6 | +18.5 |
| layer-shell Vulkan | 1.0 | 6.9 | 8.7 | 16.6 | +14.6 |

| 场景 | Plasma RSS MiB | layer-shell RSS MiB | Plasma 显存 MiB | layer-shell 显存 MiB | 整卡已用显存 MiB | 整卡 GPU 利用率 % | 板卡功耗 W |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 静态图片 | 500.0 | — | 117.1 | — | 605.4 | 0.7 | 28.9 |
| Plasma OpenGL | 535.3 | — | 186.1 | — | 674.3 | 28.9 | 39.1 |
| Plasma Vulkan | 544.0 | — | 200.3 | — | 697.8 | 37.7 | 40.5 |
| layer-shell OpenGL | 495.7 | 225.0 | 117.1 | 154.9 | 769.7 | 39.0 | 35.4 |
| layer-shell Vulkan | 493.4 | 146.2 | 117.1 | 114.3 | 728.9 | 37.9 | 35.2 |

相对静态基线，Plasma OpenGL 的 Plasma Shell 增加约 35 MiB RSS 和 69 MiB 进程显存。layer-shell 在这台机器上占用更少的桌面 CPU，整卡已用显存增量则更高。Plasma 的两个后端相差约 0.2 个 CPU 百分点，小于 Vulkan 的逐轮波动。layer-shell Vulkan 两轮都比该壳的 OpenGL 少用 CPU 和进程显存，Vulkan 的进程 RSS 两轮相差约 29 MiB。详见 [逐轮采样汇总](validation/wayland-desktop-performance-2026-11-20.csv)。

Plasma 的两个后端都绘制约 58.8 FPS。资源采样结束后，layer-shell 另开 15 秒运行记录 Wayland 帧回调，跳过第一条回调后的 5 秒，只统计应用请求的回调，排除 Vulkan 驱动的额外回调。

| layer-shell 后端 | 回调频率 | 回调间隔 P50 | P95 | P99 |
| --- | ---: | ---: | ---: | ---: |
| OpenGL | 57.90 Hz | 17.32 ms | 18.24 ms | 22.45 ms |
| Vulkan | 57.74 Hz | 17.33 ms | 18.23 ms | 20.93 ms |

绘制计数和回调节奏反映各入口的运行状态，屏幕实际呈现时间未独立计量。四种动态组合均通过桌面截图确认角色和背景可见。当前 KWin 会话中的 layer-shell 使用本地指针回落，未测全局视线跟踪的开销。结果限定在这台机器、一个模型和短时空闲桌面。2026-10-18 的测试使用不同角色、背景和采样条件，历史结果见 [原 Plasma 桌面报告](plasma-desktop-2026-10-18.md)。

测试后恢复了原虚拟桌面、壁纸配置和面板偏好。原始逐秒数据与截图保存在本地 `experiments/release_test/desktop-clean-2026-11-20/`。
