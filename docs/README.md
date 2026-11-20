# NekoMote 文档索引

本目录收录 NekoMote 的架构规范、接口契约、使用指南、各壁纸壳适配说明以及各阶段的性能与质量审计报告。

---

## 1. 核心架构与技术规范

| 文档 | 说明 |
| --- | --- |
| [AGENTS.md](../AGENTS.md) | 项目核心开发规范、架构边界、性能约束、编码约定与打磨期工作准则 |
| [ROADMAP.md](../ROADMAP.md) | 项目开发全阶段路线图（P0–P10）与各阶段完成状态记录 |
| [abi-contract.md](abi-contract.md) | 外部宿主接入的高层 C ABI (0.8.0) 契约、所有权与线程安全规范 |
| [semantics.md](semantics.md) | E-mote 模型求值语义、4×4 双三次 Bézier 变形、嵌套 0x7fc 与曲面传播 |
| [inheritance-semantics.md](inheritance-semantics.md) | 图层变换继承与遮罩继承语义分析 |
| [conventions.md](conventions.md) | 坐标空间、矩阵、时间换算、颜色/Alpha 与强类型 ID 约定 |

---

## 2. 功能与使用指南

| 文档 | 说明 |
| --- | --- |
| [configuration.md](configuration.md) | 启动配置与面板运行时偏好分离持久化机制、XDG 规范与 Schema |
| [gpu-selection.md](gpu-selection.md) | 运行时 PCI 显卡枚举 (`--list-gpus`) 与指定选卡 (`--gpu pci:...`) 指南 |
| [native-player.md](native-player.md) | 独立普通窗口播放器（`nekomote_player`）使用与诊断说明 |
| [asset-format.md](asset-format.md) | 模型与资产格式说明（schema 6 规范化模型、MessagePack + zlib 容器 `model.nkm`） |
| [ui-font-fallback.md](ui-font-fallback.md) | 共享 UI 控制面板、Fontconfig 字体发现与多语言回退机制 |
| [known-defects.md](known-defects.md) | 已知限制、待闭合边缘缺陷与环境兼容记录 |

---

## 3. 桌面适配器与渲染后端

| 文档 | 说明 |
| --- | --- |
| [plasma-dual-backend.md](plasma-dual-backend.md) | KDE Plasma 6 插件架构、OpenGL / Vulkan 双后端支持与 GPU 共享图像 |
| [plasma-distribution.md](plasma-distribution.md) | KDE Plasma 插件打包分发、跨发行版兼容性与独立安装器 `installer.py` |
| [plasma-kpackage-distribution.md](plasma-kpackage-distribution.md) | 面向 KDE Store 的标准 `.wallpaper.zip` KPackage 打包规范 |
| [layer-shell.md](layer-shell.md) | 独立 Wayland layer-shell 壳（`nekomote_layer_shell`）使用与多屏说明 |
| [gnome45.md](gnome45.md) | GNOME Shell 45+ Wayland 扩展与独立渲染宿主 |
| [x11.md](x11.md) | 独立 X11/EWMH 壳（`nekomote_x11`）使用与桌面图标共存说明 |
| [vulkan-backend.md](vulkan-backend.md) | Vulkan 1.0 渲染后端实现、WSI 呈现与生命周期规范 |
| [qt-quick-host.md](qt-quick-host.md) | Qt Quick 场景图隔离宿主架构说明 |

---

## 4. 编译、打包与自动化构建

| 文档 | 说明 |
| --- | --- |
| [building.md](building.md) | 编译环境要求、CMake 预设列表、构建与单元测试命令 |
| [dependencies.md](dependencies.md) | 固定版本依赖（FetchContent）与发行版系统依赖模式（`-DNEKOMOTE_USE_SYSTEM_DEPS=ON`） |
| [github-actions.md](github-actions.md) | GitHub Actions 多平台（Ubuntu 20/Debian 12/AlmaLinux 8）x86_64/aarch64 自动构建与发布 |
| [packaging/arch/README.md](../packaging/arch/README.md) | Arch Linux 三包 PKGBUILD 打包说明 |

---

## 5. 性能实测与审计报告

| 文档 | 说明 |
| --- | --- |
| [headless-lowend-test-2026-11-13.md](headless-lowend-test-2026-11-13.md) | **低配实机基准**：Intel Core i5-2410M / HD 3000 核显 1080p 60FPS 实测报告 |
| [wayland-desktop-performance-2026-11-20.md](wayland-desktop-performance-2026-11-20.md) | **真实桌面基准**：Plasma 6 插件和独立 layer-shell 壳的 OpenGL / Vulkan 资源开销 |
| [x11-desktop-performance-2026-11-20.md](x11-desktop-performance-2026-11-20.md) | **真实 X11 桌面基准**：Plasma 6 插件和独立 X11 壳的 OpenGL / Vulkan 资源开销 |
| [plasma-desktop-2026-10-18.md](plasma-desktop-2026-10-18.md) | **历史基准**：2026-10-18 的 Plasma 6 插件测试 |
| [memory-footprint-2026-10-12.md](memory-footprint-2026-10-12.md) | 内存开销、稳态基准与多模型切换实测报告 |
| [texture-cropping-2026-10-12.md](texture-cropping-2026-10-12.md) | PNG 图集精确裁切与显存优化报告 |
| [optimization-lessons.md](optimization-lessons.md) | 性能优化试验、避坑与回退经验汇总 |
| [wayland-compatibility-2026-10-13.md](wayland-compatibility-2026-10-13.md) | Wayland / X11 双平台兼容性修复报告 |
| [drag-inertia-2026-10-20.md](drag-inertia-2026-10-20.md) | 鼠标拖拽与软弹簧物理惯性激励优化报告 |

---

## 6. 开发历史与归档记录

早期阶段性试验日志、中间过程记录与交付报告保存在历史文档中供查阅追溯（如 `development-log.md`、`validation/` 下的冻结测试数据）。当前开发与维护请以根目录 [AGENTS.md](../AGENTS.md) 及本目录各通用规范为准。
