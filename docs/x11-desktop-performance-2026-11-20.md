# Plasma X11 桌面性能实测（2026-11-20）

同一台机器切换到原生 X11 后，使用静态壁纸作为基线，对比 Plasma 6 插件和独立 X11/EWMH 壳。两种入口分别测试 OpenGL 和 Vulkan。Wayland 数据见 [同机 Wayland 实测](wayland-desktop-performance-2026-11-20.md)。

## 环境与方法

| 项目 | 条件 |
| --- | --- |
| 系统与桌面 | Manjaro Linux x86_64，Linux 6.6.107，X.Org 1.21.1.18，Plasma / KWin 6.3.6，Qt 6.9.2，KWin OpenGL 合成开启 |
| 硬件 | Xeon E5-1680 v4（8 核 / 16 线程），RTX 3080 Ti，NVIDIA 580.82.09 |
| 显示 | DP-2 单屏，2560×1440 @ 180 Hz，缩放 1 |
| 负载 | `chocola/wintermaid-b`，静态背景 `BG_07A`，壁纸限帧 60 FPS，静音，控制面板关闭，指针保持空闲 |
| 构建 | 上轮的 Debian 12 x86_64 Plasma 6 CI 插件，同批下载的 AlmaLinux 8 x86_64 X11 壳 CI 产物 |

四种动态场景各采两轮，首尾各采一轮静态基线。每轮重启 Plasma Shell，KWin 和 Xorg 保持同一进程，
静态及独立壳场景下 Plasma 使用同一背景图片。每轮预热 20 秒，独立壳启动后先额外等待约 3 秒，
再用 [进程与 NVML 采样器](../tools/perf_compare/plasma_desktop_measure.py) 每秒采样，共 40 秒。
使用同一登录会话内的临时空白虚拟桌面，保持原面板和合成器设置。截图在资源采样区间外进行。

Plasma 与独立壳都将壁纸放在 X11 桌面层。探索阶段发现 KWin 把独立壳放在 Plasma 桌面窗口下面，角色不可见。
正式采样在进入预热前按该进程的 X11 窗口 ID 临时将独立壳抬到 Plasma 桌面窗口上方，保留 Plasma 面板。
探索结果不计入正式数据。

CPU 以一个逻辑 CPU 为 100%，桌面 CPU 包含 Plasma Shell、KWin、Xorg 和运行中的独立壳。
Wayland 报告的合计包含 Plasma Shell、KWin 和 layer-shell，X11 多计入本次实际存在的 Xorg 进程。
进程 RSS 包括共享映射，整卡显存、利用率和板卡功耗包括会话中其他程序。
Plasma 插件启用自带帧计数诊断 `NEKOMOTE_QT_PROFILE=1`，统计壁纸绘制和窗口交换。

| 产物 | SHA-256 |
| --- | --- |
| 已安装的 `libnekomote_plasma_qml.so` | `b9143dc3f1e0c8d8de8dfe66d4906fcffd189efaba3891144a5550e2e1d280e7` |
| `nekomote-x11-almalinux8-x86_64.tar.gz` | `67f21ba60d62187a9f803020528bc11d98b4d8c227411e96d186a8fc58c19571` |
| 执行的 `nekomote_x11` | `8a56cbae7ad27ed3f85ce66d5740eda171c1043cc86be5da337589b44fbf4606` |

## 结果

下表为两轮均值，增量以两轮静态基线的均值为准。

| 场景 | Plasma Shell CPU % | KWin CPU % | Xorg CPU % | 独立壳 CPU % | 桌面 CPU 合计 % | 相对静态增量（百分点） |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 静态图片 | 0.8 | 0.2 | 0.4 | — | 1.4 | — |
| Plasma OpenGL | 15.6 | 5.0 | 1.9 | — | 22.5 | +21.1 |
| Plasma Vulkan | 15.2 | 5.0 | 1.7 | — | 21.8 | +20.5 |
| X11 壳 OpenGL | 0.8 | 5.2 | 2.1 | 12.6 | 20.8 | +19.4 |
| X11 壳 Vulkan | 0.7 | 5.0 | 2.2 | 12.1 | 20.1 | +18.7 |

| 场景 | Plasma RSS MiB | 独立壳 RSS MiB | Plasma 显存 MiB | 独立壳显存 MiB | 整卡已用显存 MiB | GPU 利用率 % | 板卡功耗 W |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 静态图片 | 459.9 | — | 39.8 | — | 482.0 | 1.1 | 28.9 |
| Plasma OpenGL | 470.8 | — | 101.8 | — | 569.8 | 37.5 | 37.0 |
| Plasma Vulkan | 464.7 | — | 123.5 | — | 597.3 | 31.1 | 40.7 |
| X11 壳 OpenGL | 460.2 | 133.3 | 39.8 | 91.4 | 668.1 | 34.5 | 35.1 |
| X11 壳 Vulkan | 463.3 | 149.6 | 39.8 | 83.5 | 662.4 | 28.5 | 39.0 |

- Plasma 插件两种后端都约 **58.8 次/秒壁纸绘制、58.8 次/秒 Qt 窗口交换**。同机 Wayland 下壁纸绘制同为约 58.8 FPS，Qt 窗口交换约 117 次/秒。X11 下插件和独立壳的桌面 CPU 增量差距约 2 个百分点，较 Wayland 场景收窄。本轮没有独立测量窗口交换造成的 CPU 开销。
- NVML 将 Xorg 基线显存记为 127.4 MiB，在独立壳 OpenGL 和 Vulkan 下分别为 211.9 和 191.4 MiB。两次静态基线的整卡显存分别为 514.6 和 449.4 MiB，同期 KWin 的进程显存从 87.0 变为 21.7 MiB。独立壳 OpenGL 的两轮整卡显存均值也相差 63.4 MiB，进程显存均为 91.4 MiB。整卡均值适合描述本轮大致量级，细小差异需要结合逐轮范围判断。 [逐轮采样汇总](validation/x11-desktop-performance-2026-11-20.csv)

资源采样结束后，分别用原 CI 独立壳附加轻量记录器，统计 `glXSwapBuffers` 返回或 `vkQueuePresentKHR` 成功返回的间隔，丢弃首条记录后的 5 秒。结果每种后端包含 575 个间隔。

| 独立壳后端 | 提交频率 | 间隔 P50 | P95 | P99 |
| --- | ---: | ---: | ---: | ---: |
| OpenGL | 57.72 Hz | 17.32 ms | 17.57 ms | 17.96 ms |
| Vulkan | 58.37 Hz | 17.11 ms | 17.38 ms | 17.75 ms |

四种动态组合都通过截图确认角色和背景可见。帧计数反映应用提交节奏，未独立计量物理屏幕呈现。
结果限定于一个模型、单屏和短时空闲桌面。测试后恢复了原虚拟桌面、壁纸设置与面板偏好。
原始采样和截图保存在本地 `experiments/release_test/x11-desktop-clean-2026-11-20/`。
