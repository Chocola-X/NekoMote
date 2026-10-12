# Plasma OpenGL / Vulkan 双后端

C12，2026-10-12，基线 `cfed43e`。设置页新增“渲染后端”，默认 **OpenGL**；
选择 Vulkan 后应用设置会重建当前壁纸实例。模型、背景、UI、反应和配置继续经
当前 C ABI 0.5.0 复用。重建从保存的配置重新加载，动画和面板状态重新开始；
不是在途帧中直接替换设备。失败明确显示错误，用户可以改回 GL，不隐式降级。

## 输出、同步和线程

当前插件仍要求 **宿主 Qt Quick 使用 OpenGL 场景图**。壁纸选择 Vulkan 只改变
NekoMote 的 GPU 执行后端，不修改 plasmashell 全局 RHI。Qt 自身为 Vulkan/software
时报告不支持；原生 Qt Vulkan 场景图接入尚未实现。

`host_qt_quick/VulkanGlBridge` 拥有单独的 Vulkan 1.0 设备和一个在途帧，按当前
GL 的 deviceUUID/driverUUID 匹配设备。RGBA8 dedicated image 的 OPAQUE_FD 内存与
ready/done 二进制 semaphore 导入 GL；VK→EXTERNAL/GENERAL 释放，GL 等待后 GPU blit
到 Qt FBO，再发出 done；下一次 VK 提交在 TOP_OF_PIPE 等待并取回所有权。
ABI 仅录制模型/背景/UI，桥只提交一次，资源变更前等待 fence。resize 还等待
GL 复制完成；正常帧不使用 glFinish、queue/device idle 或 CPU 像素读回。

Vulkan framebuffer 的 render pass 与 ABI 兼容，包含两项 external dependencies，
详见 [ABI 契约](abi-contract.md)。内部行序沿用 GL；桥直接复制相同行，
item 保留既有 `mirrorVertically`，不再额外翻转。

需要 desktop GL 的 `EXT_memory_object[_fd]`、`EXT_semaphore[_fd]`，同驱动
Vulkan 的 KHR external memory/semaphore FD、dedicated allocation 与
get_memory_requirements2；还须满足 render_vk 的五位遮罩格式等要求。
这些是 Vulkan 1.0 的扩展能力，不能以“支持 Vulkan 1.0”推定全部可用。
缺能力/导入失败有明确诊断，基础 OpenGL 路线保留。

GUI 只缓冲配置和输入；Qt `synchronize` 阻塞 GUI 时复制到渲染线程。
Controller/UI、ABI 和 GPU owner 均由渲染线程串行操作。素材根目录切换重建 UI
与设备资源，避免保留旧目录图标。ABI 绘制失败后放弃未提交的命令，配置重试重建
owner。正常销毁先等 VK 工作，再销毁壁纸资源，最后等待 GL/释放桥。

## 构建与测试包

`plasma` preset 默认启用 Vulkan；仅 GL 可显式关闭，不要求 Vulkan SDK。

```sh
cmake --preset plasma
cmake --build --preset plasma --target nekomote_plasma_stage -j4
```

本轮隔离构建位于 `build/plasma-dual`，本机测试归档：
`build/evidence/plasma-dual-20261012/nekomote-plasma-local-x86_64.tar.gz`。
在 **miku 登录会话** 解压后执行随包工具（Codex 没有安装到该账户）：

```sh
tar -xzf /path/to/nekomote-plasma-local-x86_64.tar.gz
cd nekomote-plasma
python3 tools/plasma_distribution.py check-host stage
python3 tools/plasma_deploy.py --install --stage stage --prefix "$HOME/.local/share"
```

QML 环境与重新登录方式沿用 [安装说明](plasma-display-fix.md)。壁纸设置中的素材
目录填真实 `assets`，先用默认 GL，再选 Vulkan；背景和控制 UI 由共享应用绘制。
包不包含模型素材。本机包实际要求 Qt 6.9.1、glibc 2.38、GLIBCXX 3.4.31、
CXXABI 1.3.15；未来低版本 GitHub 构建见 [分发](plasma-distribution.md)，当前不能将
Arch 包宣称为通用包。

## 验收与限制

冻结摘要见 [结果](validation/plasma-dual-backend-results.json)，原始证据
`build/evidence/plasma-dual-20261012/`。Release/LTO/WERROR；本机 KDE/X11、
Qt 6.9.1、NVIDIA RTX 3060 Laptop / 575.64.03。

- 当前核心/工具/ABI CTest 180 项通过（含一个 C/GLFW 图形工具集成入口）；
  Vulkan GPU 7 用例 / 198 断言通过；无显示 ASan/UBSan ABI 3/3，真实资产项已执行。
- Qt Chocola 固定 seed 1、800×1080 的 0/60 步 GL/VK 图像逐像素一致。
  复用冻结 WebGL 0 步参照，透明图按本机 Qt 诊断背景合成后最大 RGB 误差 1/255，
  容差 2 下无超差像素。ABI 的 60 步角色/背景/展开面板 PNG 也完全一致。
- 共享背景上下方向、UI 打开/设置/切换/滚动、resize/图标拖动、
  左键长按模型拖动与滚轮缩放通过；同实例 VK→GL→VK 和录制后 UI 失败恢复通过；非 GL 宿主在切换选择后仍保持拒绝/暂停。
- 隔离 KPackage 覆盖暂停/恢复、hide/show、resize、创建销毁及配置切换；
  Khronos 普通与同步验证启用，退出/销毁日志为零错误。图像与数字分开核对。
- 分发正/负例 8 项、当前归档解压/预检及仓库内隔离 prefix 安装/verify 通过。

可安装测试不等于真实桌面全部验收。miku Vulkan、其他 GPU、Wayland、非 GL Qt
场景图、意外 device/context loss、多屏与长期运行仍未验收。没有有效原 GL context
的异常销毁不能由正常 Qt 重建测试认证。没有新性能/功耗结论；单帧在途和 CPU fence
等待不意味着 Vulkan 更快，整个 Qt 应用零分配未测。模型公式、195 全动态、面板
FPS/静音联动和 CPU 配置保存失败事务属于独立缺口。

运行新增 Vulkan Qt CTest 时需安装可用验证层；若使用隔离层则显式设置
`VK_LAYER_PATH`/其库路径。命令为
`ctest --test-dir build/plasma -R plasma_wallpaper_layers_vulkan --output-on-failure`。
测试强制请求验证层；不可用时失败，不静默记为已验证。默认发布播放不强制启用层。
