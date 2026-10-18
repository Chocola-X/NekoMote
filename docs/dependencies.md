# 依赖

默认源码依赖通过 CMake `FetchContent` 拉取并固定到具体 tag/commit；首次配置需联网。
`NEKOMOTE_USE_SYSTEM_DEPS=ON` 则全部使用发行版安装的依赖，不下载、不回退到固定版本。
两种模式共用编译目标；平台图形、字体、声音客户端始终使用系统开发包。
GitHub Actions 保持默认固定版本模式，系统模式的查找规则见 [构建说明](building.md)。

## 当前依赖

| 依赖 | 版本 | 许可 | 用途 | 模块 |
| --- | --- | --- | --- | --- |
| [nlohmann/json](https://github.com/nlohmann/json) | v3.11.3 | MIT | JSON 解析 | asset_io / importer（P1 起） |
| [fast_float](https://github.com/fastfloat/fast_float) | v6.1.6 | MIT（多重许可中选用） | 严格浮点解析 | runtime 私有场景编译 |
| [stb](https://github.com/nothings/stb) | 见 `cmake/Dependencies.cmake` 中 GIT_TAG | Public Domain（MIT fallback） | 诊断 PNG 写出、CPU 字体栅格化 | asset_io / ui |
| [Catch2](https://github.com/catchorg/Catch2) | v3.5.3 | BSL-1.0 | 测试框架 | unit / renderer / layer-shell tests |
| [GLFW](https://github.com/glfw/glfw) | 3.4 | zlib/libpng | 窗口与 GL 上下文 | apps/preview、apps/player |
| [glad](https://github.com/Dav1dde/glad) | v2.0.8 (`73db193f…`) | MIT / Khronos 注册表许可 | OpenGL 3.3 函数加载，使用自带注册表生成 | render_gl |

参考采样另用 Node.js、playwright 1.56.1（Apache-2.0，package-lock 固定）和 Python/Pillow；它们不是正式运行时依赖。图形构建生成 glad 需要 Python 与 Jinja2，未引入全局绝对路径。

## 边界约束

- 上述依赖不泄漏进 `nekomote_model` 的公共头。`nekomote_model` 仅依赖 C++ 标准库（AGENTS.md §3）。
- GLFW 仅窗口宿主 `apps/preview` / `apps/player` 使用；后续桌面适配器各自引入窗口/宿主库，不放进核心。
- Catch2 仅测试目标使用。
- JSON 用于资产/导入及应用配置；stb 用于诊断图片写出与 UI 字体准备，不在 runtime/render_gl 中解析资产或发现字体。

## 系统依赖

- CMake ≥ 3.25、Ninja、C++20 编译器（已验证 GCC 15.1.1、Clang 20.1.8）。
- OpenGL 3.3 core 兼容的驱动（仅预览需要）。
- X11 或 Wayland 显示服务器（仅预览需要；核心与测试无窗口）。

ABI/播放器的声音设备服务使用系统 libpulse≥13.99，由 `src/audio` 私有链接；
不进入模型或运行时的公共头。PulseAudio/PipeWire 服务由系统提供，本项目不安装或
启动守护进程；WAVE 解码独立实现。PNG 使用系统 libpng≥1.6；UI 必需 Fontconfig，
stb 提供字体栅格化及诊断 PNG 写出。`NEKOMOTE_BUILD_ABI=ON` 即可能需要声音依赖，
不以播放器是否启用推断整个构建没有 libpulse。

## 添加依赖的流程

1. 在 `cmake/Dependencies.cmake` 固定默认依赖版本，并在 `SystemDependencies.cmake` 提供系统依赖发现及必要 API 下限。
2. 在本文件登记版本、许可、用途和所在模块。
3. 确认不违反模块边界（AGENTS.md §3）：公共头不泄漏图形/窗口/JSON 依赖。
4. 评估许可与构建影响，记入阶段交接记录。

## 2026-10-07新增UI/C宿主依赖审查（历史记录）

- player UI 使用现有固定commit的stb_truetype及系统Fontconfig查找中文字体；需在加载阶段准备字形，字体文件及其许可与引擎代码许可分别核对。CMake目前可选查找Fontconfig、源码却必需，G6须统一必需探测/版本与模块登记。
- ABI验证宿主直接链接pulse-simple/pulse/pthread，绕开了原audio目标的pkg-config/版本约束。该实现是待收敛偏离，不扩大到runtime，也不把它登记为最终公共服务设计。G2/G6统一服务边界与构建条件。
- GLFW/Qt宿主、nlohmann JSON等具体依赖不应经ABI PUBLIC链接泄漏C++编译选项到C消费者；真实C接口头应只依赖C基础类型。
