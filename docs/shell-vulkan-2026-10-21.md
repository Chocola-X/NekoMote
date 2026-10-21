# 独立 X11 / layer-shell Vulkan 对接（2026-10-21）

基线 `4dbe7c2`。用户要求先补齐两个独立壳的 Vulkan 接入，再改造启动配置和面板配置。
本轮只增加后端选择和宿主呈现，没有开始配置格式/保存路径迁移，没有发布、安装软件
或修改真实桌面。Codex 实现、整合及隔离验证，另一个执行者完成只读边界复核。

## 实现与边界

两个壳新增 `--backend opengl|vulkan`，默认 OpenGL。构建未启用 Vulkan、设备/WSI 不支持
或初始化失败时明确报错；不隐式切回其他后端。`x11` / `layer-shell` preset 和现有基线
分发配方启用双后端；Debian12 构建镜像补齐 Vulkan 开发文件和 shader 编译器。

Vulkan 分支创建原生 Xlib / Wayland `VkSurfaceKHR`，不初始化 GLX / EGL。窗口属性、
输入区域、Wayland layer surface 的 configure/ack、缩放和帧回调仍由各自壳负责。
角色、背景、声音、面板、交互和会话全部通过现有高层 C ABI 接入：

```text
原生窗口 / layer surface
    → 宿主创建 Vulkan instance/device/surface/swapchain
    → nekomote_wallpaper_create_vk（借用设备）
    → 等待上一帧完成 → 转发输入 / resize → advance
    → nekomote_wallpaper_set_vk_target（借用本帧录制目标）
    → render_frame / render_ui → 宿主提交 / present
```

公开 ABI 仍为 0.6.0，函数签名不变。适配器不直接包含 application/ui/render_vk 的 C++
接口。共享呈现实现提取自原 `apps/abi_host/vk_host_c.*`，移到
`src/host_vulkan/host.c` / `include/nekomote/host_vulkan/host.h`；C 示例同步迁移，
旧文件删除。该目标只依赖 Vulkan 与 C 值类型，不包含壁纸业务，不进入 application。

共享宿主拥有设备、交换链、每图像呈现信号量和单帧提交；借用设备的 wallpaper 在其
销毁之前释放。输入、resize、advance 都可能触发资源替换，因此先等待上一帧完成。
退出或部分初始化失败时先停止设备工作，再依次销毁 wallpaper、Vulkan 宿主和原生输出。

正常帧先绘制 RGBA8 中间图像，再经 GPU blit 呈现，没有 GPU→CPU 读回。
PPM/PNG 只在显式诊断中读回。X11 `--capture` 仅验离屏输出，另外用无 capture 的长驻
进程和测试窗口像素验证真实 swapchain；layer-shell 即使 capture 也持续 present，
以维持 Wayland 帧回调。两个壳均支持 `--ui-open`。

## 复核中修复的问题

- `vkQueuePresentKHR` 返回 OUT_OF_DATE 时可能没有提交 Wayland buffer，旧的成功结果
  会让壳无限等待不存在的帧回调。宿主现在返回明确的重试结果；layer-shell 取消该回调，
  重建交换链并重绘同一应用状态，不重复 advance 或写 capture 文件。
- 枚举设备、扩展或 surface 格式失败时，局部临时数组可能绕过释放。统一失败清理，
  保留操作名和 VkResult，不把枚举失败当作设备不支持后继续。

`tests/host_vulkan/test_failures.c` 使用链接包装测试真实失败分支，覆盖 14 组合成场景：
设备/扩展/queue 查询错误、分配失败、格式查询失败、present 成功/SUBOPTIMAL/OUT_OF_DATE
及致命错误。断言到达目标分支、临时分配归零、surface 所有权及重试结果。这是无显示、
无真实 GPU 的测试，不计为 GPU 验收。

另有仅测试使用的 present interposer，第一次 present 直接返回 OUT_OF_DATE，确实不
向驱动提交 buffer。隔离 layer-shell 验证能够恢复；最终 4 帧画面与未注入运行逐字节
一致，Khronos 同步校验为 0 错误 / 0 警告。该动态库不链接或安装到正式程序。

## 验证证据

本机 GCC 15.1.1、Release/LTO、壳与共享宿主 Werror；固定依赖使用已有本地缓存。
KWin 6.3.6、Wayland client 1.23.1、NVIDIA RTX 3060 Laptop / 575.64.03。
基线提交、二进制和真实输入 SHA256 在
`build/evidence/shell-vulkan-20261021/metadata.json`；各工具结果记录完整 argv。

| 验证 | 结果与范围 |
| --- | --- |
| CPU CTest（排除 gpu/integration） | 既有 225 runner 全通过；新增 `host_vulkan_failures` 另行通过；配置最终的三个相关 CTest 通过 |
| CLI Vulkan ON/OFF | X11 各 7 用例 / 50 断言；layer-shell 各 6 用例 / 51 断言，关闭构建明确拒绝 Vulkan |
| Python / 构建检查 | 更新 CI/打包后 `python_tools` 通过；shell 语法、Python 语法、clang-format、diff 检查通过 |
| X11 私有 KWin/XWayland | `x11-final/results.json`，53 项场景/检查通过，包含迁移后的 C ABI 示例 |
| layer-shell Vulkan | `layer-vulkan-final/results.json`，21 项场景/检查通过 |
| layer-shell OpenGL | `layer-opengl-final/results.json`，20 项场景/检查通过 |
| 跨后端像素 | layer-shell 10 对 RGB、X11 3 对 RGB、C 示例 1 对黑底合成 RGB，全部逐字节一致 |

上述 53/21/20 是工具内的场景和检查数量，不是新增单元测试数量。

固定帧使用相同合成模型、种子和 1/60 秒步长，通常运行 4 帧。真实素材为
`azuki/casual` + `BG_07A`，包含共享面板；不据此推定全部型号通过。
分辨率覆盖 320×240、400×300、640×480、1280×720，scale 1/2。

X11 工具仅从被测进程日志取得确切 XID，验证 interactive/passthrough 的 Shape 输入
区域、实际窗口像素、每种模式四次隐藏恢复，以及 480×360→640×480 窗口尺寸变化。
这些是 ConfigureNotify/交换链重建检查，不代表 RandR 输出模式切换或物理热插拔。

layer-shell 工具验证两个虚拟输出、缺显示/协议/输出的错误、重复确定帧、真实素材/UI、
compositor 断开和 SIGTERM。运行中缩放另开启 Wayland 协议诊断，在私有会话通过
`kscreen-doctor` 改为 scale 2，再恢复 scale 1；断言客户端收到 `wl_output.scale(2)`、
发出 `set_buffer_scale(2)`，并观察 configure/ack。物理模式仍为 320×240，此检查不证明
运行中的物理分辨率变化。两个后端均通过，120 帧最终画面一致。

所有 Vulkan 成功绘制和正常销毁的诊断日志均核查校验层汇总 0/0。显示/协议发现失败
发生在设备创建前，不能拿这些负例代表 GPU 错误分支。校验层通过私有路径加载，不是
正式运行依赖；指定 `NEKOMOTE_VK_VALIDATION=1` 而缺层时明确失败。

首轮测试曾因 Unix socket 路径过长失败，改用短 `/tmp` 私有目录；运行中缩放首轮因
输出工具继承 Qt offscreen 环境而未找到输出，随后单独绑定私有 Wayland，并拒绝
返回码为 0 但内容表示失败的工具结果。保留失败证据，未将它们计为通过。

## 复现入口

```bash
# 普通启动：后端选择只影响本次运行
build/vulkan-release/bin/nekomote_x11 --assets /path/to/assets --backend vulkan
build/vulkan-release/bin/nekomote_layer_shell --assets /path/to/assets --backend vulkan

ctest --test-dir build/vulkan-release \
  -R '^(host_vulkan_failures|x11_options|layer_shell_options)$' --output-on-failure

python3 tools/testing/run_x11_test.py \
  --binary build/vulkan-release/bin/nekomote_x11 \
  --probe build/vulkan-release/bin/nekomote_x11_probe \
  --abi-host build/vulkan-release/bin/nekomote_abi_host \
  --importer build/vulkan-release/bin/nekomote_import --compositor /usr/bin/kwin_wayland \
  --real-assets /path/to/assets --evidence-dir /tmp/nkm-x11-new

python3 tools/testing/run_layer_shell_test.py \
  --binary build/vulkan-release/bin/nekomote_layer_shell --backend vulkan \
  --retry-preload build/vulkan-release/lib/libnekomote_vulkan_present_retry.so \
  --output-tool /usr/bin/kscreen-doctor \
  --importer build/vulkan-release/bin/nekomote_import --compositor /usr/bin/kwin_wayland \
  --no-protocol-server build/vulkan-release/bin/nekomote_layer_shell_no_protocol_server \
  --real-assets /path/to/assets --evidence-dir /tmp/nkm-layer-new
```

工具自行隔离 HOME/XDG/D-Bus/显示连接，获取项目 GPU 锁，不向系统窗口注入输入。
运行时缩放只作用于工具创建的虚拟输出。证据目录必须不存在；layer-shell 目录应短于
Unix socket 路径限制。校验时另指定 `NEKOMOTE_VK_VALIDATION=1` 和有效私有 layer 路径。

## 性能与剩余范围

没有修改模型求值、GPU 绘制公式或资产。新增 Vulkan 呈现沿用 C 示例的单帧在途、
RGBA8 中间图像与 GPU blit，不在稳态创建图像/交换链/信号量；重建在尺寸/WSI 失效边界。
中间图像显式占用 `width × height × 4` 字节像素存储（另计对齐、交换链和渲染资源）。
本轮未做分配采样、CPU/P95/P99、显存或功耗基准，不声明 Vulkan 比 OpenGL 更快或更省。

私有虚拟 KWin 的部分日志有 NVIDIA 图像导入/GL framebuffer 错误；客户端固定帧、
Vulkan 校验、帧回调和 X11 窗口像素通过，不代表 layer-shell 的最终合成像素通过。
真实 Sway/Hyprland/niri、原生 Xorg 登录、其他 WM/驱动、真实输入/声音、多物理屏、
热插拔、长时间运行仍需独立验收；XFCE 图标覆盖问题未改变。

基线 CI 配方和 Arch 描述已同步，固定 Ubuntu20 / Debian12 镜像重建、远端 Actions、
ARM64 和标签发布本轮未执行。接下来再实施已讨论的“显式保存启动配置 + 自动保存
面板配置”，Plasma 启动设置仍由插件管理；不要把本次后端参数误认为已持久化。
