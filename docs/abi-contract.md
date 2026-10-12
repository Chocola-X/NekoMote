# C ABI 当前契约与验证

当前仅支持 **0.5.0**，入口为 `include/nekomote/abi.h`。开发期不保留旧签名；宿主重新编译。模式与错误码使用 `int32_t`，接受值由常量定义，非法值通过错误结果拒绝。这样 C/C++ 都能安全处理未知整数，不依赖 C++ 枚举的取值范围或编译器 enum 布局。

## 壁纸应用层接口

共享 Controller/UI 与高层 `nekomote_wallpaper_*` 已实现，GLM 已迁移 Plasma 调用者。本轮显示补修见 [说明](plasma-display-fix.md)。宿主只经 ABI 连接共享业务；以下既有单会话/renderer 契约仍适用于相应接口，不能据此推定高层实例已完成全部异常/生命周期验收。

高层壁纸输入使用输出物理像素、左上原点、Y 向下；Qt 壳按逻辑/输出尺寸转换一次。转发左键按下/释放后，由共享 Controller 区分未移动点击与按住左键移动超过 4px 后立即拖动；不再额外对同一次左键按下调用 touch。直接 touch 接口保留给明确的触摸/诊断事件。滚轮在 UI 外围绕指针缩放，右键留给桌面。pointer_move/mouse_button 的 `timestamp_ms` 为采集时的单调毫秒时间，同一实例使用一致时钟，按序传递；当前手势按移动量判定，时间戳仍按宿主单调时钟传入。宿主必须按当前头文件重建，不保留旧签名。`set_render_target` 指定 FBO，角色/背景和 UI 都绘制到此目标；UI 可在输出尺寸晚于加载时延迟初始化。显式 model/session 诊断模式不绘控制面板。创建/加载/绘制/重置/销毁中的 GPU 操作须在其有效 GL 上下文与允许线程执行；CPU 查询不能作为跨线程读写许可。当前其他高层接口的异常/借用寿命边界仍需独立复核，不沿用测试数量宣称全部满足。

后续公共数据和资源ID保持后端无关；现有GL renderer与GL输出目标是GL专属入口，不能通过改名包装成Vulkan目标。GL/Vulkan 设备、输出和 native 同步分别在对应后端契约中定义；CPU业务与UI不接收GL/Vulkan句柄。当前headless/ASan核心路径继续不引入UI GPU或窗口构建依赖。

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

`NEKOMOTE_BUILD_ABI=ON` 独立于 `NEKOMOTE_BUILD_PREVIEW`。ABI 的函数指针 GL 后端可以编译而不创建上下文、窗口或配置 GLFW/OpenGL 系统库。共享声音服务需要 libpulse ≥16.1 开发包；构建不连接声音服务器。只有 GPU 正例需要显示环境。

```sh
cmake --preset asan
cmake --build --preset asan -j 6
env -u DISPLAY -u WAYLAND_DISPLAY ctest --preset asan
```

asan preset 关闭 preview/player，启用 ABI。`abi_negative_test` 是纯 C 的 NULL/无效 loader/重复初始化失败测试；`abi_synthetic_boundary` 使用仓库的小型合成 fixture、有效程序和会话，检查非法模式999、NaN、尺寸、帧失效、背景和 OOM 诊断。它们不依赖真实模型或显示服务。`abi_test` 为真实资产集成入口；缺少文件时返回77、CTest 明确 Skipped；文件存在但损坏仍失败。

早期边界验证记录在 `build/evidence/integration-2026-10-07/`：独立副本不含 assets/refer，headless + ABI + 无离线工具构建通过，134 PASS、1 个真实资产集成项 Skipped。使用明确的本地已固定依赖缓存，未测试网络重新下载。本段数量只属于当期边界检查，不用于当前完整测试计数。

GPU 负例使用真正的 renderer：纹理长度/尺寸、非法 blend/protection/composition、NULL 非空帧、复制 OOM，失败后白色宿主目标保持白色，重新 prepare 后可绘制。预热后100次 ABI prepare/render 普通 C++ new 为0；合成 CPU session 的120次 advance/prepare/frame/background 查询为0。这些不包含驱动 malloc、初始化、截图或整个 UI 的分配。

## Vulkan 后端契约

- `create_vk` 借用宿主的 instance、physical device、device 和 graphics queue；实例只拥有自己的 GPU 资源。当前颜色格式只接受 `VK_FORMAT_R8G8B8A8_UNORM`，其他格式明确失败。所有 native 句柄必须来自同一有效设备，非空检查不证明 native 句柄合法。
- 所有实例调用在同一求值/图形线程串行执行。宿主须在 `advance`、加载、resize、reset、输入引起资源变更和销毁前等待此前引用实例资源的全部 GPU 工作完成。设备线程共享 queue 时还须按 Vulkan 规则外部串行同步；仅同线程录制并不足以证明 GPU 完成。
- 销毁顺序：停止录制/新提交 → 等待引用壁纸资源的 GPU 工作 → `nekomote_wallpaper_destroy(wp)` → 宿主释放设备/实例。不能先销毁实例后等待 fence。
- 宿主在 `advance` 后设置本帧 `nekomote_vk_target`，检查 setter 的 `nekomote_error`，随后 `render_frame` + `render_ui` 可共享 target。`advance`、输出尺寸变化、GPU reset 使旧 target 失效；宿主提交以后也不得继续使用旧借用目标，下一帧重新设置。失败 setter 会使之前的 target 失效。
- target 借用一个正在录制、处于 render pass 外的 primary command buffer，以及兼容的单颜色 framebuffer：RGBA8 UNORM、sample count 1、attachment 已处于 COLOR_ATTACHMENT_OPTIMAL，render pass 的 LOAD/STORE 和 initial/final layout 同为 COLOR_ATTACHMENT_OPTIMAL。target 正尺寸必须与 `set_output_size` 的当前尺寸一致。 framebuffer 创建使用一个 graphics subpass、color attachment 0，无 resolve/depth；依赖也必须兼容：EXTERNAL→0 为 ALL_COMMANDS/MEMORY_READ|MEMORY_WRITE → COLOR_ATTACHMENT_OUTPUT/COLOR_ATTACHMENT_READ|WRITE，0→EXTERNAL 为 COLOR_ATTACHMENT_OUTPUT/COLOR_ATTACHMENT_WRITE → ALL_COMMANDS/MEMORY_READ|MEMORY_WRITE，两项 dependencyFlags=0。只匹配格式不满足 render pass 兼容性。
- 壁纸绘制自行开始/结束兼容 render pass，仅录制命令。提交、帧完成 fence、输出采样与呈现属于宿主。加载、字形/图标内容变化可执行独立同步上传；不能把录制入口的约束解释为所有接口都不会提交上传。
- 内部输出行方向沿用 OpenGL：底部逻辑行位于图像存储起始处。宿主用于屏幕呈现/上左原点截图时翻转一次；共享目标的外部采样必须采用对应 UV 约定，不能在模型投影重复翻转。
- 首次 `render_frame` 会清理目标或先绘背景，不要求宿主每帧另行清零。UI 在其后保留颜色；未设置有效 target 时绘制返回 RENDERER 错误。
- 当前 result 型高层壁纸入口捕获 C++ 异常，OOM 返回 OUT_OF_MEMORY，其他后端异常返回可检查错误与固定 TLS 诊断。GPU 候选在上传、prepare、resize 成功后才替换当前 GPU owner；背景资源同样先准备后替换。宿主遇到绘制失败须放弃或明确处理本次未提交命令，不能作为成功帧呈现。
- 没有 Vulkan 构建支持时 `create_vk` 返回 NULL + UNSUPPORTED；无效必需指针/颜色格式返回 INVALID_INPUT。setter 在有效 OpenGL 壁纸上返回 UNSUPPORTED。ABI 开发期签名已升级，所有调用者重建，不保留旧签名。

C12 独立复核（2026-10-12）证据位于 `build/evidence/plasma-dual-20261012/abi-validation.json`：ABI CPU CTest 3/3 通过，真实资产项已执行；新增合成 GPU ABI 用例 39 assertions 通过；60 固定步的巧克力、背景和展开面板 GL/Vulkan PNG 完全一致，普通与同步验证未报告错误。修前 Vulkan PNG 重复翻转导致整体倒置，保留 `before-*` 对照；`--ui-open` 现通过共享 `set_ui_open` 实际展开面板。以上不覆盖驱动失败注入或全部型号动态认证。

## 尚未认证

C/GLFW 宿主已经收敛为显式静音诊断工具，G8 确定步 PNG 与 C++ 像素一致；它不代表实际桌面插件，也不承担正式音频服务。Qt Quick和隔离Plasma包已有后续证据，G27部署也已交付；不将它们列为未实现。实际桌面/驱动上下文丢失/多屏/长期及新版完整壁纸ABI仍未最终验收，当前范围见[摘要](review-agents-current.md)。
