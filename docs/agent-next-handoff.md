# 当前独立任务交接

2026-10-12，C12 基线 `cfed43e`。player、Vulkan ABI 0.5.0、Plasma 双后端已有源码与隔离验收；先读 [当前摘要](review-agents-current.md)、[Plasma 后端](plasma-dual-backend.md)。不要重新实现 G31/G32 或修改已通过的模型公式。

## GLM：G33 基线分发构建

范围为 `.github/workflows/plasma-distribution.yml`、`tools/plasma_baseline*`、`tools/plasma_distribution.py` 和对应分发测试/文档。先声明实际 base 与文件；不同时修改 ABI、Qt/Vulkan 桥或模型求值。

1. 在可用 Docker/Podman 环境运行 [分发配方](plasma-distribution.md)。固定镜像与 apt snapshot，不换成最新 Ubuntu 来绕过低版本编译问题。
2. 分别完成 x86_64/aarch64 构建。无相应环境时记未执行；若运行 GitHub 工作流，记录 run 链接、源码提交、日志与归档 hash，不推送/发布 release 或操作真实桌面，除非用户另行授权。
3. 两份包必须通过 `--baseline-debian12`，保留实际 Qt 版本、ELF requirements、相对 RPATH、依赖许可和 toolchain 清单。不得降低报告中的 Qt 版本骗过门槛。
4. 运行八项分发正/负例、actionlint 与 bash 语法检查。兼容版本、缺库、错误架构、篡改 manifest 必须明确拒绝。归档在隔离目录解压，用随包工具预检/安装/verify，不修改真实用户配置。
5. 仅修复可复现的构建/打包问题，记录失败和修复前后；涉及公开 ABI 或 GPU 行为的调整先交 Codex 复核。构建成功不等于跨发行版 GPU 验收。

验收：两个架构的真实日志、兼容审计、归档 hash、负例与隔离安装均可复核；无法执行项单列。本机 Arch 包不满足基线，这是预期拒绝。

## Gemini：V21 固定条件视觉检查

复用 C12 证据，不修改 GPU 同步/公式。固定模型、输出/DPI、seed/步数及同一资产来源；先检查 Chocola 的 GL/VK UI/背景和上下方向，再抽核 Fraise 与 Milk 的遮罩/触摸覆盖。数值比较使用既有容差，不以“看起来相近”放行。

在用户安排的 miku 会话中可核对 OpenGL 默认、Vulkan 选择、背景/完整设置面板、左键长按拖动、滚轮缩放、resize/暂停恢复与改回 GL。只用自身应用事件或用户操作，不向系统窗口注入输入，不自行注销/重启桌面。记录 GPU/驱动、X11/Wayland、实际 Qt 场景后端和每项证据；缺少共享图像扩展时验证错误提示与 GL 可恢复，不能记 Vulkan 成功。源码/固定图像与真实桌面分别统计。

## Codex 保留项

复核基线包与跨模块改动；Qt 非 OpenGL 场景图、意外 device/context loss、角色渐变预算和 CPU 配置保存失败事务仍未闭合。面板/FPS/声音收拢另需明确契约后再分派；不让 GLM 重新猜测同步或模型语义。C8 的 3 bust_LR20、嵌套/2 标量继续保留，触摸候选不应用。

GPU 用 `build/gpu-validation.lock` 串行，冻结二进制/hash/输入；外部素材只读。每阶段验证后提交，填写真实 Agent/Model/Context；未执行、候选与 PASS 分开。
