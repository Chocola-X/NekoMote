# 依赖

源码依赖通过 CMake `FetchContent` 拉取并固定到具体 tag/commit；平台驱动和声音客户端使用明确的系统开发包。首次配置需联网；后续构建复用已拉取源码。

## 当前依赖

| 依赖 | 版本 | 许可 | 用途 | 模块 |
| --- | --- | --- | --- | --- |
| [nlohmann/json](https://github.com/nlohmann/json) | v3.11.3 | MIT | JSON 解析 | asset_io / importer（P1 起） |
| [stb](https://github.com/nothings/stb) | 见 `cmake/Dependencies.cmake` 中 GIT_TAG | Public Domain（MIT fallback） | PNG/图像解码 | asset_io（P2 起） |
| [Catch2](https://github.com/catchorg/Catch2) | v3.5.3 | BSL-1.0 | 单元测试框架 | tests/unit |
| [GLFW](https://github.com/glfw/glfw) | 3.4 | zlib/libpng | 窗口与 GL 上下文 | apps/preview、apps/player |

| [glad](https://github.com/Dav1dde/glad) | v2.0.8 (`73db193f…`) | MIT / Khronos 注册表许可 | OpenGL 3.3 函数加载，使用自带注册表生成 | render_gl |

参考采样另用 Node.js、playwright 1.56.1（Apache-2.0，package-lock 固定）和 Python/Pillow；它们不是正式运行时依赖。图形构建生成 glad 需要 Python 与 Jinja2，未引入全局绝对路径。

## 边界约束

- 上述依赖不泄漏进 `nekomote_model` 的公共头。`nekomote_model` 仅依赖 C++ 标准库（AGENTS.md §3）。
- GLFW 仅窗口宿主 `apps/preview` / `apps/player` 使用；后续桌面适配器各自引入窗口/宿主库，不放进核心。
- Catch2 仅 `tests/unit` 使用。
- nlohmann/json 与 stb 限 `asset_io`/`importer` 使用，不进入 `runtime`/`render_gl` 热路径。

## 系统依赖

- CMake ≥ 3.25、Ninja、C++20 编译器（已验证 GCC 15.1.1、Clang 20.1.8）。
- OpenGL 3.3 core 兼容的驱动（仅预览需要）。
- X11 或 Wayland 显示服务器（仅预览需要；核心与测试无窗口）。

普通播放器引入系统 libpulse 17.x（CMake 要求 >=17 且 <18），仅 `src/audio` 私有链接，不进入核心或其公共头；用途是异步设备流、位置计时和暂停/停止，替代在求值线程执行阻塞声音 I/O。动态库 libpulse 为 LGPL-2.1-or-later；本项目不复制其源码。`NEKOMOTE_BUILD_PLAYER=OFF` 不配置该依赖。实际开发包 17.0-43-g3e2bb，使用标准 `<pulse/pulseaudio.h>` 和 pkg-config，不硬编码本机路径。服务协议由系统 PulseAudio/PipeWire 提供；应用不安装或启动守护进程。WAVE 解码独立实现，无额外音频编解码依赖。

## 添加依赖的流程

1. 在 `cmake/Dependencies.cmake` 用 `FetchContent_Declare` 固定到具体 tag/commit。
2. 在本文件登记版本、许可、用途和所在模块。
3. 确认不违反模块边界（AGENTS.md §3）：公共头不泄漏图形/窗口/JSON 依赖。
4. 评估许可与构建影响，记入阶段交接记录。
