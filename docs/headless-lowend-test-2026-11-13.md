# 无头低配实机测试：i5-2410M / HD 3000

2026-11-13，执行者 Codex。源码 `6b8fcb864e036daa68efbae38e53edb3e9ff7b37`，
未修改生产代码。本轮测试的是目标机原生构建的播放器和运行时，不是 Plasma 插件。

## 结论和范围

Debian 12 满足本轮构建及 OpenGL 运行要求。Intel HD 3000 **真实参与渲染**：
播放器报告 Intel / Mesa SNB GT2，GLX 报告 `Accelerated: yes`，独立
`intel_gpu_top` 诊断采到 Render/3D 忙碌率最高 76.98%。没有使用 Xvfb 或软件渲染替代。

三款模型的 15 场 OpenGL 播放均成功退出。1080p / 60 FPS 配置下实际帧循环约
58.9–59.1 FPS，播放器约占单逻辑核 26%–30%，折算四逻辑核全机约 6.4%–7.6%，
采样 RSS 约 80 MiB。Xorg 另占单核约 6.4%–6.7%；这些不是整个桌面的 CPU 数据。

CPU 用例 288 通过、1 跳过；OpenGL 用例 28/28 通过。早期附加的 GLES 检查
27/28 通过，有一项像素阈值失败，详情见下文。用户随后明确只需 OpenGL，未继续
尝试 Vulkan；不将 Vulkan 软件设备的结果当作核显性能。

没有物理显示器或活动输出。以上 FPS 是播放器在 X11 上的提交/等待循环节奏，
不能证明物理屏幕扫描输出、合成器呈现或完整桌面壁纸性能。每场播放约十几至二十几秒，
不作为长期稳定性、资源泄漏或功耗认证。

## 目标系统和准备

| 项目 | 实测配置 |
| --- | --- |
| CPU | Intel Core i5-2410M，2 核 4 线程，标称 2.30 GHz |
| 内存 | 系统报告 3.7 GiB；8.8 GiB swap，测试结束未使用 |
| GPU | Intel HD Graphics 3000 / Sandy Bridge，PCI 8086:0116，共享内存 |
| 系统 | Debian 12 bookworm，glibc 2.36，内核 6.1.0-50-amd64 |
| 驱动 | Mesa 22.3.6，OpenGL core 3.3 / GLSL 3.30，GLES 3.0 |
| 构建 | GCC 12.2.0、CMake 3.25.1、Ninja、C++20、Release/O3/LTO |
| 频率策略 | 保持 schedutil，未锁定 CPU/GPU 频率 |
| 显示 | 复用现有 SDDM Xorg `:0`；三个接口均 disconnected |

原系统 AlmaLinux 8.10 / glibc 2.28 的预检确实未通过：现有 Ubuntu20 基线包原生库
实际 `dlopen` 报 `GLIBC_2.29 not found`，并要求系统缺少的 GLIBCXX 版本。
这里依据的是 ELF 实际需求和远端加载失败，不把 CI 符号上限当成最低运行要求。
当时没有开始性能测试；用户重装 Debian 12 后重新检查并继续。

Debian 初次更新软件源因系统时钟落后而报 Release 文件尚未生效。校正时间、启用
NTP 后正常安装开发依赖，没有关闭软件源时间/签名校验。固定第三方源码从本地已有缓存
随 `git archive HEAD` 上传，未改依赖版本。临时代理只监听回环地址，验证连通后已停止，
没有将代理开销带入性能采样。

用任务目录中的 Xauthority 副本访问 Xorg，并临时为 rin 添加 renderD128 访问 ACL。
X11 framebuffer 从 1024×768 调整为 1920×1080；无须新建软件虚拟显卡或启动第二个 Xorg。
测试结束恢复 framebuffer、移除临时 ACL、删除认证副本及代理配置，无测试进程残留。
开发依赖及 `/home/rin/nekomote-test-2026-11-13/` 中的构建和结果保留供复测。

构建设置 `NEKOMOTE_WERROR=ON`，所选目标成功构建；GCC12 的 LTO 链接测试时仍输出
`test_asset_validation.cpp:171` 所在 vector 插入路径的一条 `-Wstringop-overflow` 警告。
本轮没有改测试/禁用警告，也不宣称构建零警告。

## 正确性检查

| 检查 | 用例结果 | 说明 |
| --- | --- | --- |
| `nekomote_unit_tests` | 288 通过 / 1 跳过 | 跳过项要求未提供的 E-mote 源素材环境变量 |
| `nekomote_render_tests` / OpenGL | 28 通过 | 原阈值、真实 Intel GL 上下文 |
| 同一 GPU runner / GLES | 27 通过 / 1 失败 | 未放宽阈值；不作为本轮 OpenGL 不通过 |
| 三型号 1080p 固定步截图 | 3 次成功 | seed=1、120 步、fit、静音；不混入性能测量 |
| 真实播放器测量 | 15 场成功 | 每场实际 600 个采样帧，120 帧预热 |

首次 CPU 测试有 5 项 Controller 加载失败：我为额外四个 CPU 型号只传模型与会话，
使资产目录扫描发现不完整型号。补齐其纹理/元数据后完整重跑通过。首次失败保留在原始
证据 `cpu-tests-incomplete-fixture.xml`，不是项目回归，也没有通过过滤用例取得通过。

GLES 失败用例 `GPU atlas orientation, alpha, host target and resize` 在 resize 后
要求 `sample[0] < 4`，本机读回为 4；其余该处已执行断言通过。选择同一用例额外重跑
三次均在该处失败。尚未区分驱动过滤精度差异和项目实现问题，不宣布 GLES 完整兼容。
证据见 [失败复现](validation/headless-lowend-2026-11-13/gles-reproduction.txt)。

七型号 × 待机/拖动视线共 14 工况的 CPU 帧指纹，在本机三轮内全部一致。
与原开发机 GCC15 构建的指纹并非逐位一致，进一步比较每工况 10 个采样帧，合计 140 帧：
整数协议字段（数量、索引、命令、遮罩等）一致，UV 和颜色数值一致；坐标最大绝对差
为 x=0.000244140625、y=0.00048828125 模型单位。没有用新阈值把逐位差异判为通过，
也没有据此宣称跨驱动最终像素完全相同；具体编译器/数学库成因未单独归因。
见 [逐字段结果](validation/headless-lowend-2026-11-13/cross-host-frames.json)。

## CPU 求值性能

沿用仓库的 `docs/validation/compound-review-2026-11-08/runtime_probe.cpp`，
原生 Release/O3/LTO 链接。固定 CPU0，种子 12345、1/60 秒步长，120 步预热、
600 步采样、三轮；表中为每工况三轮对应分位数的中位数。
`Session advance + prepare_frame` 包含动画、物理和最终构帧，不含纹理加载、GPU、
字体/UI、声音设备和呈现。独立 geometry 计时不能与嵌套 Session 时间相加。

| 型号 | 待机 P50/P95/P99（ms） | 拖动和视线 P50/P95/P99（ms） |
| --- | --- | --- |
| chocola/date-a | 0.560 / 0.582 / 0.618 | 0.696 / 0.757 / 0.767 |
| chocola/wintermaid | 0.630 / 0.652 / 0.690 | 0.783 / 0.849 / 0.865 |
| chocola/casual-b-no-bell | 0.437 / 0.507 / 0.546 | 0.614 / 0.676 / 0.726 |
| vanilla/wintermaid | 0.638 / 0.657 / 0.701 | 0.792 / 0.858 / 0.867 |
| maple/yukata | 0.475 / 0.546 / 0.586 | 0.613 / 0.679 / 0.737 |
| coconut/casual | 0.497 / 0.576 / 0.617 | 0.665 / 0.733 / 0.790 |
| azuki/casual | 0.657 / 0.671 / 0.703 | 0.775 / 0.838 / 0.848 |

42 次工况运行在探针覆盖的预热后 Session/geometry 路径，常规 C++ `new/new[]`
计数均为 0；不包括驱动内部 malloc、应用/UI/音频线程或整个播放器的分配。

## OpenGL 播放性能

单模型、fit、平色背景、静音、seed=1，UI 未展开，无鼠标注入。1080p/60 FPS 每型号
三次，720p/60 FPS 和 1080p/30 FPS 每型号各一次。性能采样在编译完成后进行，
并与截图、GPU 监控及其他测试分开运行。
播放器内置指标预热 120 帧后记录 600 帧；`/proc` 从进程启动 6 秒后每 250ms 采样。
CPU 百分比按 ticks/墙钟统计；表中“单核”指占满一个逻辑 CPU 为 100%。

1080p/60 FPS 三轮中位数：

| 型号 | 循环 FPS | CPU advance+prepare+GL submission P50/P95/P99（ms） | 播放器单核 CPU | Xorg 单核 CPU | 播放器采样峰值 RSS（MiB） |
| --- | --- | --- | --- | --- | --- |
| chocola/date-a | 59.053 | 3.301 / 3.481 / 3.880 | 25.71% | 6.56% | 79.66 |
| chocola/wintermaid | 58.978 | 4.093 / 4.230 / 4.599 | 30.46% | 6.69% | 79.32 |
| coconut/casual | 58.886 | 3.204 / 3.558 / 3.957 | 25.94% | 6.39% | 79.70 |

CPU 指标是项目求值、CPU 侧 GL 提交及其可能等待的合计，**不是 GPU 执行完成时间**。
未调用 glFinish/readback 做性能同步。帧循环分位数见 JSON，不能把 CPU 提交耗时直接
换算成最大 GPU FPS。播放器+Xorg 合计约为全机 8.1%–9.3%，未包含其他系统进程，
也没有扣除登录管理器/Xorg 空载成本。

附加工况，仅一次，不给出稳定跨场景收益结论：

| 型号 | 720p/60：FPS / 播放器单核 CPU | 1080p/30：FPS / 播放器单核 CPU |
| --- | --- | --- |
| chocola/date-a | 58.929 / 26.00% | 29.720 / 14.12% |
| chocola/wintermaid | 58.910 / 28.41% | 29.718 / 15.57% |
| coconut/casual | 58.924 / 25.60% | 29.719 / 14.01% |

这一组中，降低帧率比降低分辨率更明显地减少 CPU 使用。但没有测总线功耗、实际电源
输入或可靠的独占显存；HD 3000 的共享内存也不能直接按 RSS 解释。结束时 swap 使用为 0，
系统仍有约 2.9 GiB available；这些不等于长期内存无增长。

## 证据和复现

- [环境、源码/输入/二进制哈希](validation/headless-lowend-2026-11-13/environment.json)
- [用例汇总](validation/headless-lowend-2026-11-13/test-summary.json)
- [CPU 分项汇总](validation/headless-lowend-2026-11-13/runtime-summary.json)，同目录
  `runtime-{1,2,3}.jsonl` 保留三轮原始输出。
- [播放器逐场汇总](validation/headless-lowend-2026-11-13/player-summary.json)
- [播放器采样工具](validation/headless-lowend-2026-11-13/measure_player.py)，
  [CPU 快照对照工具](validation/headless-lowend-2026-11-13/compare_frames.py)。

完整构建/测试日志、GPU 监控、截图、每场 /proc 原始样本及 CPU 二进制快照保存在
`experiments/headless-lowend-2026-11-13/debian12/`，该目录被忽略，不属于版本化证据。
模型/图片不纳入 Git。复测需显式准备资产和可访问的硬件 X11 上下文；不运行软件
渲染模拟、不注入系统输入。示例（在目标机的 src 目录）：

```bash
export DISPLAY=:0 XAUTHORITY=/path/to/authorized/Xauthority
./build/bin/nekomote_unit_tests
./build/bin/nekomote_render_tests
taskset -c 0 ../runtime-probe "$PWD/assets" 600
python3 /path/to/measure_player.py "$PWD/build/bin/nekomote_player" "$PWD/assets" ../results
```

未验收 Plasma5/6、Wayland 壳、物理屏幕呈现、声音/触摸设备、后台遮挡策略、全部型号
及长期运行。本轮没有性能优化前后对照，不据此宣布任何优化百分比。
