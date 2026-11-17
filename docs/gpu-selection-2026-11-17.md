# GPU1：独立宿主启动选卡验收

2026-11-17，基线 `d6a8e38`。按用户确认的讨论结论实现，使用方法见
[GPU 选择说明](gpu-selection.md)。`--gpu pci:dddd:bb:dd.f` 只作用于当前进程；
`--list-gpus` 不读取配置、不要求资产或显示服务。Plasma 继续跟随宿主。

## 实现边界

新增小型 `host_gpu` 宿主工具，集中处理严格 PCI 标识、硬件发现、OpenGL 初始化环境
和真实设备核验，不依赖 Vulkan。三个入口只在初始化阶段调用，不向 application、UI、
runtime 或渲染协议添加设备策略。两个配置 schema 不变；GPU 与 `--save-config` 同用时
明确拒绝，避免把临时参数误当已保存设置。

OpenGL 在窗口库初始化前准备 Mesa PCI DRI_PRIME 或 NVIDIA offload。EGL 通过 DRM
render node 关联 PCI，NVIDIA 可使用实际 GL 设备 UUID 关联驱动信息；可取得的 UUID
与 EGL 身份冲突时拒绝显式选择。Mesa GLX 仅在 vendor/device 能唯一对应 PCI 时核验，
同型号多卡歧义明确失败。多个 NVIDIA 的 OpenGL provider 映射暂不支持。默认启动不修改
驱动环境，身份查询失败只报告可取得信息，不剥夺原来的默认运行能力。

Vulkan 保持 1.0，按可用性启用 KHR properties2，查询 EXT pci_bus_info 精确匹配目标，
并检查当前 surface 的 graphics/present 队列。目标缺失、无法识别或无法呈现均不回退。
player 复用共享选择器，删除其重复选择循环；不改变原来的独显优先默认评分。
surface 所有权仍归原宿主；仓库内 C sample、render tests 同步迁移。

实现经其他执行者交叉复核，检查默认行为、身份验证、扩展加载和 player surface 所有权。
参考官方 [Mesa 环境变量](https://docs.mesa3d.org/envvars.html) 和
[NVIDIA PRIME Render Offload](https://download.nvidia.com/XFree86/Linux-x86_64/570.169/README/primerenderoffload.html)，
Vulkan 身份能力核对 [Khronos 设备规范源码](https://github.com/KhronosGroup/Vulkan-Docs/blob/main/chapters/devsandqueues.adoc)。
文档检索工具不可用时通过 curl 读取原始文档；没有移植驱动源码。

## 本机验证

硬件为 NVIDIA RTX 3060 Laptop GPU `pci:0000:01:00.0` 和 AMD Cezanne
`pci:0000:06:00.0`，Linux 6.12.39、NVIDIA 575.64.03、Mesa 25.1.6。
Release/LTO/Werror，真实素材 `chocola/date-a`，私有 KWin 虚拟 640×480 输出和 XWayland，
player 自身 320×240。测试无系统输入注入，不修改用户桌面或会话。

系统最初只安装 NVIDIA ICD：三个入口的 AMD Vulkan 请求均明确失败，没有误选独显。
为验证真实双卡，从 Arch Linux 官方归档取得与现有 Mesa 配套的
`vulkan-radeon 1:25.1.6-1`，仅解包到项目证据目录，用进程级 `VK_DRIVER_FILES` 同时指定
隔离 RADV 与系统 NVIDIA ICD；未安装系统包。核显 Vulkan 成功证据均使用这个隔离驱动。

| 检查 | 结果 |
| --- | --- |
| 三入口 × 两后端 × 默认/独显/核显/不存在设备 | 24/24；18 次成功渲染、6 次按预期拒绝，日志核验实际 PCI |
| 固定 4 帧客户端图像 | 同入口同后端默认与显式选择同卡逐字节一致；跨卡最大通道误差 3/255，平均每通道不超过 0.248/255；所有图像有实际角色像素 |
| Vulkan validation | player 三次成功生命周期 errors=0；两壳八次生命周期（含拒绝目标）errors=0 / warnings=0 |
| Vulkan 合成选择/失败 | 原有14场景和新增12场景共26通过；含反序枚举、同型号不同PCI、目标不能呈现、地址/扩展缺失与默认评分 |
| 共享 OpenGL CPU 契约 | PCI 格式、sysfs 发现排序/过滤、同型号歧义、身份不明/错误/冲突、默认环境保留通过 |
| 全部必选 CPU CTest | 最终259/259 runner通过；含player进程配置测试与两壳CLI测试，不重复累计集合内用例 |
| OpenGL-only | 三个程序Release/LTO/Werror构建通过；4/4配置/CLI/共享GPU检查通过，无Vulkan链接依赖 |
| 旧发行工具链定向检查 | GCC/G++10.5、binutils2.34、glibc2.31；实际Vulkan CMake目标和共享GPU C++20模块编译、执行通过 |
| 格式/差异 | 20个C/C++文件clang-format检查、Python语法、git diff --check通过 |

旧工具链检查使用项目内解包的 sysroot，Vulkan/Linux UAPI 头来自本机，不冒充完整的
Ubuntu20/Debian12容器工作流、整包严格审计或ARM64验收。

## 短时运行对照与限制

另在同一私有Wayland输出，以相同模型/尺寸/60FPS、120帧预热后采样120帧，默认和分别
指定两卡各跑一次，不启用validation或截图。六次均完成，循环节奏59.535–59.864FPS。
默认OpenGL实际使用AMD，CPU P50 0.669ms；显式同卡0.661ms。默认Vulkan实际使用NVIDIA，
CPU P50 0.707ms；显式同卡0.684ms。这些是短样本波动，不宣称选卡参数带来性能改善。
详细P50/P95/P99和逐次节奏见机器记录。测量仅覆盖player CPU求值及GL提交/VK录制，
不把循环节奏当显示器最终呈现计数，没有测量整个桌面方案功耗、显存或长期资源占用。

没有新增稳态枚举、匹配、查询或日志；设备解析及验证均发生在初始化边界。
未验证Sway/Hyprland/niri、原生Xorg登录、其他驱动、多独显、ARM64、远端CI、真实多屏、
显式选卡后的热插拔/设备丢失恢复或长期运行。图像证据为客户端输出，不代表合成器最终
像素验收。Plasma没有独立选卡或修改宿主环境。

可提交机器记录：[gpu-selection-2026-11-17.json](validation/gpu-selection-2026-11-17.json)。
完整本地日志、二进制哈希、截图、隔离驱动及失败证据位于
`build/evidence/gpu-selection-20261117/`。首轮matrix记录系统缺RADV的失败，
`final-matrix/`记录最终双卡驱动矩阵，`measurement/`为独立短时对照，不混算两轮结果。

执行与复核：Codex，GPT-6家族（精确变体未暴露）。未推送或发布。
