# C ABI 当前契约与验证

当前仅支持 **0.4.0**，入口为 `include/nekomote/abi.h`。开发期不保留旧签名；宿主重新编译。模式与错误码使用 `int32_t`，接受值由常量定义，非法值通过错误结果拒绝。这样 C/C++ 都能安全处理未知整数，不依赖 C++ 枚举的取值范围或编译器 enum 布局。


## 壁纸应用层接口

共享 Controller/UI 与高层 `nekomote_wallpaper_*` 已实现，GLM 已迁移 Plasma 调用者。本轮显示补修见 [说明](plasma-display-fix.md)。宿主只经 ABI 连接共享业务；以下既有单会话/renderer 契约仍适用于相应接口，不能据此推定高层实例已完成全部异常/生命周期验收。

高层壁纸输入使用输出物理像素、左上原点、Y 向下；Qt 壳按逻辑/输出尺寸转换一次。转发左键按下/释放后，由共享 Controller 区分短按触摸与长按 300ms 拖动；不再额外对同一次左键按下调用 touch。直接 touch 接口保留给明确的触摸/诊断事件。滚轮在 UI 外围绕指针缩放，右键留给桌面。pointer_move/mouse_button 的 `timestamp_ms` 为采集时的单调毫秒时间，同一实例使用一致时钟，按序传递；延迟批次仍按事件时间识别长按。宿主必须按当前头文件重建，不保留旧签名。`set_render_target` 指定 FBO，角色/背景和 UI 都绘制到此目标；UI 可在输出尺寸晚于加载时延迟初始化。显式 model/session 诊断模式不绘控制面板。创建/加载/绘制/重置/销毁中的 GPU 操作须在其有效 GL 上下文与允许线程执行；CPU 查询不能作为跨线程读写许可。当前其他高层接口的异常/借用寿命边界仍需独立复核，不沿用测试数量宣称全部满足。

后续公共数据和资源ID保持后端无关；现有GL renderer与GL输出目标是GL专属入口，不能通过改名包装成Vulkan目标。未来设备/输出/native同步在对应后端契约中定义；CPU业务与UI不接收GL/Vulkan句柄。图形入口可按实际后端新增，不提前制造通用handle联合体或空Vulkan接口。当前headless/ASan核心路径继续不引入UI GPU或窗口构建依赖。

## 所有权与失败

输入 gaze/touch 均为呈现方向的中心 framebuffer 像素，Y 向下。宿主转换逻辑窗口/DPI 并减去半个画幅，不再翻转 X；session 负责作者触摸区的缩放、放置与镜像逆变换。诊断及本轮验证见 [C6 输入交接](c6-input-review-handoff.md)。

- CPU session由一个求值线程顺序调用，不能并发advance/prepare/输入；immutable program可供各自独立的session借用。
- model/session_asset 可在 program 编译成功后销毁；program 必须比借用它的 session 活得更久。GPU renderer 与 CPU session 分别拥有资源；renderer 创建、调用和销毁都要求宿主提供同一有效上下文及图形线程。
- C/C++ 帧逐字段复制到各自预分配的存储，不互转对象指针。session 创建时按 scene 容量预分配主帧，背景预留 8 顶点/12 索引/2 命令；renderer reserve 同时预留复制缓冲区和 GPU 容量。
- `upload_texture` 输入为紧密排列的 RGBA8，新增 `pixel_bytes` 必须等于 width × height × 4；图像加载后的实际长度由 `nekomote_image_byte_count` 取得。宿主负责保证指针在声明的长度内可读。
- renderer `prepare` 失败后，`render` 明确失败且不绘制旧帧；下一次成功 prepare 恢复。此策略与 resize/upload 的资源操作失败分开：未成功修改的资源不会自动使整个 renderer 永久 fault。
- 复制、预分配和初始化的异常封闭于 C 边界。线程本地诊断使用固定 1024 字节缓冲区，OOM 报告不再分配；过长诊断截断，错误码保留。成功调用不清除此前的错误；需要时显式 `nekomote_clear_error`。
- 帧/字符串/像素的借用有效期以 header 为准；模型推进后查询 frame 不再返回上次复制的陈旧模型帧。背景与模型共用 250ms 时间预算，暂停时均不推进。

## 构建与已验证范围

`NEKOMOTE_BUILD_ABI=ON` 独立于 `NEKOMOTE_BUILD_PREVIEW`。ABI 的函数指针 GL 后端可以编译而不创建上下文、窗口或配置 GLFW/OpenGL 系统库。只有 GPU 正例需要显示环境。

```sh
cmake --preset asan
cmake --build --preset asan -j 6
env -u DISPLAY -u WAYLAND_DISPLAY ctest --preset asan
```

asan preset 关闭 preview/player，启用 ABI。`abi_negative_test` 是纯 C 的 NULL/无效 loader/重复初始化失败测试；`abi_synthetic_boundary` 使用仓库的小型合成 fixture、有效程序和会话，检查非法模式999、NaN、尺寸、帧失效、背景和 OOM 诊断。它们不依赖真实模型或显示服务。`abi_test` 为真实资产集成入口；缺少文件时返回77、CTest 明确 Skipped；文件存在但损坏仍失败。

早期边界验证记录在 `build/evidence/integration-2026-10-07/`：独立副本不含 assets/refer，headless + ABI + 无离线工具构建通过，134 PASS、1 个真实资产集成项 Skipped。使用明确的本地已固定依赖缓存，未测试网络重新下载。本段数量只属于当期边界检查，不用于当前完整测试计数。

GPU 负例使用真正的 renderer：纹理长度/尺寸、非法 blend/protection/composition、NULL 非空帧、复制 OOM，失败后白色宿主目标保持白色，重新 prepare 后可绘制。预热后100次 ABI prepare/render 普通 C++ new 为0；合成 CPU session 的120次 advance/prepare/frame/background 查询为0。这些不包含驱动 malloc、初始化、截图或整个 UI 的分配。

## Vulkan 后端契约

### 句柄所有权与寿命

- `nekomote_vk_device_context` 中的 `instance`/`physical_device`/`device`/`graphics_queue` 由宿主创建并拥有。`nekomote_wallpaper_create_vk` 借用这些句柄，不创建或销毁设备。
- `nekomote_wallpaper` 实例及其内部 GPU 资源（纹理、缓冲、管线）必须在宿主销毁 Vulkan 设备之前通过 `nekomote_wallpaper_destroy` 销毁。
- 销毁顺序：`nekomote_wallpaper_destroy(wp)` → `vkDeviceWaitIdle` → 宿主释放 Vulkan 设备/实例。

### 帧借用契约

- `nekomote_vk_target` 是每帧借用的录制目标。宿主必须在每次 `render_frame` / `render_ui` 调用前通过 `nekomote_wallpaper_set_vk_target` 设置当帧 target。
- target 的 `commands` 必须处于 `VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT` 录制状态，且 `framebuffer` 的 color attachment 已转换为 `VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL`。
- 壁纸实例只向 `commands` 录制绘制命令，不提交（`vkQueueSubmit`）、不呈现（`vkQueuePresentKHR`）、不等待 fence。提交和呈现由宿主负责。

### 线程约束

- 同一 `nekomote_wallpaper` 实例的 `render_frame` / `render_ui` / `set_vk_target` 必须在同一线程调用（串行求值线程）。
- 宿主的 `vkQueueSubmit` / `vkQueuePresentKHR` 可在另一线程，但必须通过 fence/semaphore 保证与壁纸实例的命令录制有明确的 happens-before 关系。

### 错误处理

- `nekomote_wallpaper_create_vk` 在 Vulkan 未编译时返回 NULL + `NEKOMOTE_ERROR_UNSUPPORTED`。
- device context 任一成员为 NULL 时返回 NULL + `NEKOMOTE_ERROR_INVALID_INPUT`。
- `render_frame` / `render_ui` 在未设置 target 时返回 `NEKOMOTE_ERROR_RENDERER`。

### 格式约定

- `color_format` 当前固定为 `VK_FORMAT_R8G8B8A8_UNORM`。
- 宿主 render pass 的 attachment loadOp/storeOp 为 LOAD/STORE，initialLayout/finalLayout 为 COLOR_ATTACHMENT_OPTIMAL。
- 壁纸实例假定 target 内容在 render_frame 首次写入前已被宿主清零或由背景写入覆盖。

## 尚未认证

C/GLFW 宿主已经收敛为显式静音诊断工具，G8 确定步 PNG 与 C++ 像素一致；它不代表实际桌面插件，也不承担正式音频服务。Qt Quick和隔离Plasma包已有后续证据，G27部署也已交付；不将它们列为未实现。实际桌面/驱动上下文丢失/多屏/长期及新版完整壁纸ABI仍未最终验收，当前范围见[摘要](review-agents-current.md)。
