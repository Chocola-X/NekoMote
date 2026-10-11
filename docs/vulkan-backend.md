# OpenGL / Vulkan 1.0 双后端

本轮 C11 从 `b733626` 复核并重写 Vulkan 播放链。application、runtime、CPU UI 和模型求值共用；player 的 GPU 执行可选择 OpenGL 或 Vulkan。Plasma 与当前 C ABI 仍使用 OpenGL，本轮没有迁移其宿主。

原实现的问题集中在执行链，而非双后端方向：宿主等待未提交的 fence、present 等待未发出的 semaphore，渲染器另行提交却不等待 acquire；背景/UI 未完整接线，字形/图标/clip 存在空实现；截图没有执行 GPU 复制，descriptor 按帧增长，资源可能在提交完成前释放。C11 统一宿主提交所有权并替换这些路径，没有保留实验实现的兼容入口。

## 构建和选择

需要 Vulkan loader/开发头、`glslangValidator` 与支持 Vulkan 的驱动；shader 在构建时生成，运行时不调用编译器。设备 API 和 SPIR-V 均以 Vulkan 1.0 为目标。

```sh
cmake --preset vulkan-release
cmake --build --preset vulkan-release -j4
build/vulkan-release/bin/nekomote_player --assets assets --backend vulkan
build/vulkan-release/bin/nekomote_player --assets assets --backend opengl
```

两个命令运行同一二进制，共用模型、背景、配置及控制。默认 OpenGL；选择在启动时生效。`--backend` 只接受 `opengl` / `vulkan`，原实验参数 `--vulkan` 已删除。也可在现有 release 输出目录启用双栈：

```sh
cmake --preset release -DNEKOMOTE_BUILD_VULKAN=ON
cmake --build --preset release -j4
build/release/bin/nekomote_player --assets assets --backend vulkan
```

不启用 Vulkan 时，OpenGL 与无窗口核心构建不要求 Vulkan SDK。图形负例/图像测试需要显示服务器，不能把没有执行的 GPU 检查记作通过。

## 所有权和帧协议

`VkHost` 创建实例、设备、输出、交换链、command buffer 和同步对象。它使用一个在途帧：在 GPU 资源修改/复用前等待上一提交完成；模型、背景、过渡角色、触摸覆盖和 UI 向同一个 command buffer 录制；宿主只提交一次并呈现。渲染器不独立提交、不持有呈现 semaphore、不决定业务切换。

显示帧的提交在 TOP_OF_PIPE 等待 acquire semaphore，使 UNDEFINED 图像布局转换也在图像获取之后执行，发出当前交换链图像的完成 semaphore，并关联宿主 fence。完成 semaphore 按交换链图像持有，图像重新 acquire 后才能复用。fence 在对应提交之前才 reset。窗口 resize / out-of-date 时由宿主重建交换链。资源释放和替换需要此前提交完成；当前帧已录制的资源也不得提前销毁。模型候选先完整准备 CPU/GPU/背景/音频资源，再提交切换；失败保留当前角色资源。

渲染目标使用 RGBA8 UNORM 中间图像，屏幕呈现由 GPU blit 完成。当前呈现明确要求 RGBA/BGRA8 UNORM 表面；仅有 sRGB 格式的宿主暂时拒绝，避免 blit 编码转换改变 GL 参照颜色。没有每帧 GPU→CPU readback。截图是独立诊断：真实 image→buffer 复制、完成等待、映射、将预乘 RGBA 转为直通 alpha PNG。内部目标采用与 GL 一致的逻辑行序和负 Y 投影，以保持浮点窗口坐标舍入、边界覆盖与遮罩整数裁剪一致；viewport 仍为 Vulkan 1.0 的正高度。最终呈现通过一次 GPU blit 翻转 Y；诊断截图翻转读回行，再转为 PNG。UI 的逻辑 clip 在后端转为目标行坐标，跨中间 pass 不再重复翻转。

后端消费公共 `RenderFrame` / `ui::Painter`，图形句柄只存在 render_vk 与 player 宿主边界。UI 图标采用后端本地 token，字体 CPU 度量与布局不依赖 Vulkan。字形/图标更新发生在显式内容变更边界；正常帧复用顶点缓冲、descriptor 和遮罩图像。

遮罩保留 GL 的五位量化语义；按设备能力选择五位绿色通道的 16-bit 格式，当前 NVIDIA 使用 A1R5G5B5。缺少所需格式/颜色混合/采样能力时明确拒绝，不能静默换成不同精度。保护颜色、Multiply、遮罩和共享 group clip 按 GL 顺序执行。透明图元及 UI 保序，字体图集 padding 明确清零。

## 验收与范围

冻结输入、二进制、截图、GPU 检查及性能结果见 [C11 结果](validation/vulkan-backend-results.json)。Debug 宿主在验证层可用时启用它；Release 默认关闭验证层。GPU fixture 显式请求可用验证层，报告必须说明是否实际启用。验证层错误、像素门槛与目视检查分别记录。

本轮核心 172 用例 / 42662 断言、GL GPU 12 / 221、Vulkan GPU 6 / 159 和 C ABI 3 项通过。六组真实模型/共享 UI/触摸覆盖 GL–VK 截图均通过冻结容差 2；三种模型的动态截图逐像素一致。复用此前固定输入的 Chocola WebGL 初态图，alpha 与黑白底合成误差为 0。实际窗口回调覆盖切换/渐变、失败保留、resize、最小化恢复和关闭，Vulkan 同步验证错误为 0。完整设置面板截图已目视检查；这不等于全部模型及交互均已视觉验收。

本机测试使用支持较新 Vulkan 的 NVIDIA 驱动，以 Vulkan 1.0 API / shader 目标执行；不等于已测试所有 Vulkan 1.0 硬件。首版单帧在途保证资源所有权清晰，暂不承诺 Vulkan 比 OpenGL 更快。已测零分配仅覆盖预热后的 renderer/UI/宿主帧路径普通 C++ new（120 帧）；不含 aligned new、C malloc/驱动、其他线程或截图。整应用分配、声音设备、长期运行、195 全动态及 Plasma Vulkan 各自验收，不由局部 GPU 通过推定。

Release 采样使用 Chocola、BG_07A、完整设置面板，800×1080、静音、30 FPS，预热 120 帧后取 180 帧。OpenGL CPU 阶段包含 draw 提交；Vulkan 阶段记录 command buffer，队列提交/上一帧等待在该阶段计时外，两者不能直接换算性能提升。GL 测量入口使用软件 FPS 预算（swap interval 0，避免此机 EGL 非焦点窗口的 vblank 等待停滞）；正常 GL 播放仍使用原有 vsync，Vulkan 使用 FIFO。实际提交/呈现完成与显示扫描输出没有单独 GPU 时间戳测量，loop cadence 只报告循环吞吐。功耗、显存与长时性能未测。

同步检查不能只跑 offscreen：需同时运行窗口 `--check-window` 覆盖 acquire、呈现、resize、最小化恢复与交换链重建。安装 Khronos 验证层后可设置 `VK_LAYER_ENABLES=VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT`，再运行 GPU fixture 与窗口检查。本轮曾捕获并修复布局转换早于 acquire 等待的同步冲突，普通 core validation 未报告它；所有 GPU 门槛需检查进程返回码及销毁日志。
