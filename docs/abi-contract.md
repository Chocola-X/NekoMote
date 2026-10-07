# C ABI 当前契约与验证

当前仅支持 **0.3.0**，入口为 `include/nekomote/abi.h`。开发期不保留旧签名；宿主重新编译。模式与错误码使用 `int32_t`，接受值由常量定义，非法值通过错误结果拒绝。这样 C/C++ 都能安全处理未知整数，不依赖 C++ 枚举的取值范围或编译器 enum 布局。

## 所有权与失败

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

本轮 dev 136/136；无 DISPLAY/WAYLAND_DISPLAY 的 ASan/UBSan 135/135。另从当前源码导出不含 assets/refer 的独立副本，headless + ABI + 无离线工具构建通过：134 PASS、1 个真实资产集成项 Skipped。使用明确的本地已固定依赖缓存，未测试网络重新下载。证据目录：`build/evidence/integration-2026-10-07/`。

GPU 负例使用真正的 renderer：纹理长度/尺寸、非法 blend/protection/composition、NULL 非空帧、复制 OOM，失败后白色宿主目标保持白色，重新 prepare 后可绘制。预热后100次 ABI prepare/render 普通 C++ new 为0；合成 CPU session 的120次 advance/prepare/frame/background 查询为0。这些不包含驱动 malloc、初始化、截图或整个 UI 的分配。

## 尚未认证

C/GLFW 宿主仍有重复音频服务、pthread 所有权、加载失败和错误处理缺陷，不能代表 ABI 的正确用法或正式壁纸应用。G2 必须精简/迁移，不能再复制音频服务。部分 CPU 运行错误的诊断仍为通用文字，下一步补源节点/字段路径及更完整失败注入。上下文丢失/宿主卸载、多线程生命周期、实际 Qt Quick/Plasma 宿主以及最终共享库安装没有验收。
