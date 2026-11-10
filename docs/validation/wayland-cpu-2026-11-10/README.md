# 帧调度验证证据

基线263f3eb，最终实现见[报告](../../wayland-cpu-audit-2026-11-10.md)。
构建 Release/O3/LTO/Werror，Qt6.9.2/GCC15.2.1/NVIDIA580.82.09。
生产库、两版probe、模型/会话与当前工具源码的SHA256见[hashes.json](hashes.json)。
裸数据为[performance.json](performance.json)，每个phase独立保存wall、进程CPU、
真实壁纸draw计数、GUI timer次数、提交间隔P50/P95/P99、进程peak RSS。

Wayland/GLES与XWayland/desktop GL各三轮交替；不限帧每轮两段。
Wayland/desktop GL和应用Vulkan各一轮、各两段不限帧，只作为诊断覆盖，不作稳定性认证。

| 平台 / Qt上下文 / 应用后端 | 工况 | CPU：基线→最终（单核=100%） | 变化 | 壁纸绘制 FPS：基线→最终 |
| --- | --- | --- | --- | --- |
| wayland/gles/opengl | limited60 | 16.27% → 15.09% | -7.21% | 58.71 → 58.81 |
| wayland/gles/opengl | paused_redundant | 10.10% → 6.44% | -36.20% | 61.52 → 0.00 |
| wayland/gles/opengl | unlimited | 102.30% → 12.83% | -87.46% | 60.02 → 59.96 |
| xcb/desktop/opengl | limited60 | 9.73% → 9.16% | -5.83% | 58.95 → 58.86 |
| xcb/desktop/opengl | paused_redundant | 4.93% → 2.92% | -40.83% | 61.98 → 0.00 |
| xcb/desktop/opengl | unlimited | 37.40% → 11.24% | -69.93% | 59.88 → 60.05 |
| wayland/desktop/opengl | limited60 | 16.32% → 14.83% | -9.16% | 58.58 → 58.49 |
| wayland/desktop/opengl | paused_redundant | 8.63% → 6.23% | -27.80% | 61.32 → 0.00 |
| wayland/desktop/opengl | unlimited | 104.17% → 10.81% | -89.62% | 60.02 → 60.10 |
| wayland/gles/vulkan | limited60 | 16.63% → 14.73% | -11.47% | 59.00 → 58.64 |
| wayland/gles/vulkan | paused_redundant | 9.77% → 7.77% | -20.47% | 61.43 → 0.00 |
| wayland/gles/vulkan | unlimited | 106.91% → 13.25% | -87.61% | 60.02 → 60.08 |

这些CPU是独立Qt宿主进程，不含KWin、plasmashell其他部件或整个桌面。
paused_redundant有5ms人为update；0绘制不等于Qt窗口没有合成/呈现，正常外层暂停本来已停timer。
不限帧Wayland/GLES基线每秒平均420553个timer tick，最终0；XWayland为122330→0。
两边实际draw约60FPS；原假设的自动重绘推断已由Qt的renderPending门槛代码否定，
本轮没有直接测量真实180Hz输出。

Wayland/GLES不限帧的提交间隔均值P50/P95/P99为16.43/17.49/18.23ms→
16.58/17.77/18.67ms；P99增加0.44ms，CPU下降87.46%，draw FPS为60.02→59.96，
按整体成本接受局部抖动。间隔来自GUI交换通知，样本短，不能当最终扫描输出或长尾认证。
60FPS的CPU均值本机下降7.21%/5.83%（Wayland/XWayland），不承诺其他宿主或驱动同幅收益。

正常60FPS的peak RSS范围：Wayland/GLES基线293.8–297.1MiB、最终294.7–298.6MiB；
XWayland/desktop GL为195.3–198.6MiB、195.4–197.3MiB。进程峰值RSS相近，未单独测稳态驻留。
没有独立测GPU时间、显存、功耗、原生Xorg、真实Plasma/180Hz或长期稳定性。
匹配desktop GL后Wayland的残余CPU差异仍存在，当前证据不足以把它定性为Plasma架构原因。

## 已执行的检查

- Qt6共享插件/宿主与新runner Release/LTO/Werror构建通过。
- Qt5.15.17共享插件同模式构建通过；未声称Qt5.12/GCC10或远端CI验证。
- 五份`final-check-*.log`全部PASS：Wayland GLES/desktop GL、Wayland应用Vulkan、
  XWayland desktop GL/应用Vulkan。含180/240/0/60/0、暂停/恢复、隐藏/恢复、
  resize、scene-graph重建、暂停模型重载，以及真正动态画面0→60→0/隐藏恢复检查。
- 现有Python Qt宿主14个测试方法中，13个通过，生命周期方法的desktop/GLES两个
  子例SIGSEGV。冻结基线在同环境复现这两项，见[基线日志](baseline-qt-lifecycle.log)
  和[最终日志](candidate-qt-host.log)。未计PASS、未更改/跳过原测试；该诊断问题仍需单独定位。
- 新架构/生命周期改动经过另一执行者只读复核，指出的暂停重载同步边界已修复并回归。
- Vulkan同步验证另通过已注册的CTest入口，见[vulkan-validation.log](vulkan-validation.log)，
  未出现Validation Error或VUID。系统缺少验证层，使用临时目录解出的Debian12
  `vulkan-validationlayers_1.3.239.0-2_amd64.deb`，通过VK_LAYER_PATH/LD_LIBRARY_PATH
  仅为测试进程启用；没有安装系统包。下载源为
  [Debian官方包](https://deb.debian.org/debian/pool/main/v/vulkan-validationlayers/vulkan-validationlayers_1.3.239.0-2_amd64.deb)。

## 未保留的初版

[pilot-performance.json](pilot-performance.json)保存对全部模式连接frameSwapped时的首轮短预热数据。
XWayland有限60FPS CPU均值7.802%→8.295%（+6.3%），不限帧仍明显获益。
最终只在不限帧时连接，去掉有限帧不必要的GUI队列工作，并使用2秒预热重跑；
不能混用两轮绝对CPU数字或把差异全部归因为通知开销。
初版生产库hash亦保留在hashes.json，原冻结二进制/脚本在本机临时审计目录。
