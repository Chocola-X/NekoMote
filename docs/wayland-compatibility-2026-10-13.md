# C17 Wayland / X11 兼容修复

2026-10-13，基线 `0c0f400a3206835d5e0d94b46308883385c408f5`。用户结束优化任务，要求原生 Wayland、保留 X11 和修复 Wayland 下 Plasma 渲染失败。Codex / GPT-6（精确变体未暴露）实现，同轮另一 Codex 执行者独立复核加载器、纹理采样、Qt 生命周期和 Vulkan 共享边界。机器记录见 [结果](validation/wayland-compatibility-results.json)。

## 复现与修复

GLFW 3.4 已有平台自动选择：两个平台均编译时，根据 `XDG_SESSION_TYPE` 及可用显示服务器选择 Wayland 或 X11。本机修复前 player / abi_host 已能创建原生 Wayland 输出。Linux 构建现在明确包含两个平台，player、preview 和 abi_host 在启动时报告 `Window platform=wayland|x11`，后两者也保留或补齐 GLFW 错误回调。没有通过清空 `DISPLAY` 强制模式，也没有改变 GLFW 的自动选择策略。

鼠标样式改变不能证明 XWayland。固定版本 GLFW 的 Wayland 游标来自 `XCURSOR_THEME` / `XCURSOR_SIZE`，缺失时使用库默认主题和大小；它没有 Qt 的桌面游标集成。实际平台应以启动日志为准。本轮未实现额外游标主题协议。

Qt Wayland 默认可能创建 **OpenGL ES** 上下文。本机 Plasma 风格测试实际得到 NVIDIA GLES 3.2；原后端只接受 desktop GL 3.3，使用 GLES 不支持的 rectangle sampler。另一个问题是独立 Qt host / probe 请求 3.3 CoreProfile，却未指定 renderable type；本机 Qt 将它作为 ES 请求，产生 `EGL_BAD_MATCH (0x3009)`，无法创建渲染上下文。

修复按实际边界处理：

- `render_gl` 按当前上下文装载 desktop GL 3.3 或 GLES 3.0，模型、背景及 UI shader 使用相应 GLSL 版本；仅在支持时操作 sRGB 写控制。
- desktop GL 保留 rectangle texture。GLES 使用 2D texture，在 GPU 上将裁切后的存储尺寸补齐到 2 的幂，按实际裁切边界 clamp 后采样；CPU 像素仍保持精确裁切。
- Plasma 使用宿主实际 GL / GLES 上下文，不修改 Plasma 全局图形设置。Vulkan → GL 共享图像接入允许符合条件的 GLES，仍要求全部 memory/semaphore FD 扩展、设备 UUID 匹配和原有双向 GPU 同步。缺少能力时继续明确报错。
- 独立 Qt host / probe 明确请求 desktop GL；host 增加诊断 `--gl-api desktop|gles`、实际上下文日志和 scene graph 初始化失败报告。
- Qt 新建 renderer 在首次 `synchronize()` 必须复制 GUI 配置快照，即使共享 dirty 标志已被上一 renderer 消费。配置仍只在 GUI 被 Qt 阻塞的同步边界复制，修复上下文重建后模型资源不可用的问题。

普通 normalized sampler 搭配任意裁切尺寸的候选，在本机复杂遮罩上出现最大 3/255 偏差。手动过滤和量化候选没有进入最终实现；最终 POT 存储恢复硬件过滤精度，未扩大画面门槛。

## 验证条件与结果

Release / LTO / WERROR，GCC 15.1.1，Ryzen 7 5800H，RTX 3060 Laptop，NVIDIA 575.64.03，Qt 6.9.1，Plasma 6.3.6。用户当前真实登录会话为 Wayland，`DISPLAY=:1` 为 XWayland。输入验证仅使用项目诊断回调；Plasma 契约测试窗口拒绝焦点和系统指针输入，其他 Qt 图形验收使用隔离虚拟输出，不向其他窗口注入事件。GPU 检查串行使用 `build/gpu-validation.lock`。

| 验证 | 结果及范围 |
| --- | --- |
| 全量相关构建 | Release / LTO / WERROR 通过，包括 GLFW 壳、ABI、Qt / Plasma 和 Vulkan |
| 无窗口核心 / 工具 | 189/189；真实素材导入已执行，无素材 Skipped |
| desktop GL GPU | 15 用例、275 断言通过 |
| GLES GPU | 同一 15 用例、265 断言通过；ES 不执行 rectangle namespace 专属断言 |
| Vulkan GPU | 9 用例、348 断言通过；三个最终 CTest runner 3/3 通过 |
| player / abi_host 基线对照 | 2 壳 × 2 平台 × 2 后端，8 对图像逐字节一致；800×1080、seed 1、60 步，UI 开启，静音、不保存配置 |
| Qt desktop GL / GLES 对照 | 5 个真实模型 × 初态 / 60 步，10 对逐字节一致；800×1080、seed 1；最终二进制复测 |
| Qt 原生 Wayland | 隔离 KWin 下 Python 14/14；含 desktop / GLES 两个生命周期子用例、resize / recreate、DPI 1.5、CLI 负例及真实素材截图 |
| Qt X11 API | 当前 XWayland 上生命周期测试 1/1，包含 desktop / GLES 两个子用例；重建画面与初始画面逐字节一致，创建 / 销毁各 2 次 |
| Plasma 原生 Wayland | 隔离 KWin 下 4/4 CTest，真实 GLES 3.2；GL / Vulkan 角色、背景、UI、后端来回切换、暂停 / 隐藏 / 输入 / 释放及配置拒绝；Vulkan 同步验证错误 0 |
| Plasma X11 API | 当前 XWayland 下同一组 CTest 4/4，desktop GL |
| 真实 KPackage | 隔离原生 Wayland 下 6/6；捕获、错误资产、非法配置、5 轮配置生命周期、DPI 1.5 和错误恢复 |
| 分发包 | 最终 stage / ELF / 相对 RPATH / manifest 审计及本机只读 host 预检通过，归档已生成 |

Qt 对照模型为 `chocola/date-a`、`chocola/maid-a-no-bell`、`vanilla/date-a`、`fraise/casual-a`、`azuki/casual`。请求过的 `coconut/date-a` 本地不存在，明确跳过，没有计入这 10 对。Plasma KPackage 使用 `coconut/casual`。

当前桌面的普通测试窗口被其他窗口完全遮挡时，Qt Wayland 不再收到 frame callback，`isExposed()` 变为 false；DPI 1.5 窗口还受真实屏幕尺寸限制。保留这些失败日志，最终采用仓库内私有 runtime / config / cache 和私有 DBus 的虚拟 KWin 输出 2560×2160 验证，不删除应用暂停判断、不修改用户桌面配置。Qt 生命周期最初还复现了 renderer 重建配置丢失，修复后 desktop / GLES 均通过。

KPackage GPU 测试加载构建树插件。额外设置 stage QML 路径后，import trace 确认 probe 内部路径仍优先选择构建树；该轮不作为 stage 插件的 GPU 验收。stage 的 ELF / manifest / RPATH 审计和 GPU 内容验收分别记录。

## 资源影响和未覆盖项

这是兼容修复，没有进行新的稳态 CPU / GPU / 功耗性能采样。desktop GL 和 Vulkan 的精确裁切存储保持原行为。GLES POT padding 增加显存容量需求；202 张既有素材图集的理论 RGBA 容量如下，输入 hash 与 C15 记录一致：

| 存储方式 | 汇总 RGBA 字节 | 相对原图减少 |
| --- | ---: | ---: |
| 原图 | 6,296,633,344 | — |
| CPU 精确裁切 / desktop GL / Vulkan 图集 | 4,489,676,856 | 28.697% |
| GLES POT 图集 | 6,262,882,304 | 0.536% |

199/202 张需要 padding。以上是素材集合的容量求和，不是同时加载模型的内存，也不是驱动实测显存；不包括输出目标、遮罩、UI 及驱动开销。GLES 保留 CPU 裁切收益，但不能沿用 C15 desktop GL 的显存收益结论。

没有修改或安装用户桌面插件；当前真实 plasmashell 中升级后的完整壁纸仍待安装后验收。原生 X11 登录会话、AMD / Intel / Mesa、缺少共享扩展的设备、多屏与 8–24h 长期运行未执行。本机归档依赖 Qt 6.9 / GLIBC 2.38 / GLIBCXX 3.4.31 / CXXABI 1.3.15，**不是 Debian 12 基线包**；跨发行版任务仍单独待办。

## 复现入口

标准构建入口仍为 `cmake --preset plasma` / `cmake --build --preset plasma`。本轮证据构建目录 `build/optimization-current` 使用相同 Release / LTO / WERROR 配置并打开 ABI、Qt、Plasma 和 Vulkan。图形断言可运行：

```bash
ctest --test-dir build/optimization-current -R '^renderer_(gles_|vulkan_)?gpu$' -j1 --output-on-failure
```

不要在该 CTest 外再套 flock：各 runner 已持有项目 GPU 锁。Qt 诊断可通过 `QT_QPA_PLATFORM=wayland` / `xcb` 与 `--gl-api desktop|gles` 组合核对；GLFW 壳默认按会话选择，无需 Qt 平台环境变量。Qt / Plasma GPU 测试需要可呈现且尺寸足够的输出；虚拟 KWin 的私有环境、运行脚本、日志、冻结二进制、图像及 hash 均保留在忽略目录 `build/evidence/wayland-20261013/`，具体输入和结果见机器记录。

测试归档：`build/evidence/wayland-20261013/nekomote-plasma-wayland-x86_64.tar.gz`。在用户明确要求安装之前，不运行会修改桌面配置的 reinstall 脚本。

GLFW 自动选择及游标逻辑可在固定 3.4 依赖的 `src/platform.c` / `src/wl_init.c` 复核；Qt 遮挡处理参照 [Qt 6.9.1 官方实现](https://code.qt.io/cgit/qt/qtwayland.git/tree/src/client/qwaylandwindow.cpp?h=v6.9.1)。
