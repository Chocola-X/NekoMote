# 当前独立任务交接

C18（2026-10-14）已提交 `94af01c`，见 [合成模型求值报告](synthetic-semantics-2026-10-14.md)：51 组程序生成模型、4 组真实回归及2份PsBuild产物通过。C19 已尝试 cc，15格最小对照有4组NaN，尚未建立有效布局/公式，正式求值继续拒绝；复现、限定结论与下一步见[cc调查](mesh-cc-investigation-2026-10-14.md)。不重新推测已验证公式。

2026-10-12，C12 基线 `cfed43e`。player、Vulkan ABI 0.6.0、Plasma 双后端已有源码与隔离验收；先读 [当前摘要](review-agents-current.md)、[Plasma 后端](plasma-dual-backend.md)。不要重新实现 G31/G32 或修改已通过的模型公式。

## GLM：G34 重复几何编译消除（当前优先）

C13 优化之后接手，输入与验收按 [优化建议](optimization-suggestions-2026-10-12.md) 的 G34。声明新 HEAD 与文件；复用一次编译场景并维持唯一所有权、变量来源发现与动画/物理借用寿命。冻结加载性能、CPU/真实会话及同条件截图；不调整模型公式、GPU 同步、错误检查、ABI 借用范围。大量模型的证据准备由 GLM 执行，图像异常交 Gemini，编译契约与边界交 Codex。

当前优化基线已推进至 C15 `958a404`，C16 索引/UV 与局部渲染状态复用已实测，见 [说明](frame-optimization-2026-10-13.md)。接手先确认新 HEAD；G34 仍是加载期独立任务，不能将 C16 的小幅 CPU 收益当作显存/功耗或 8–24h 验收。Plasma FPS、角色变换与面板位置保存已实现，仍需在真实桌面验收。

## GLM：G33 基线分发构建

最新用户反馈（2026-10-15）：已验证 GitHub CI 产物中的本体、KDE5、KDE6 正常，授权自动发布。新增 `release.yml` 在稳定版本标签下复用三套构建，本体扩展原生 ARM64，发布五份包及其校验值。先按 [标签发布说明](github-actions.md#版本标签自动发布) 检查首次远端运行；本体 ARM64 和发布流程本身仍待实际执行。下文保留早期验收方法及证据来源，已获用户确认的使用结果不要重新列为完全未验证。

2026-10-15 配方已调整为 `.github/workflows/native-linux.yml`、`plasma5.yml`、`plasma6.yml`，详见 [CI 说明与本地证据](github-actions.md)。旧 `plasma-distribution.yml` 和 `plasma_baseline_build.sh` 已由三个工作流与 `tools/ci_build.sh` 替代。接手先检查未提交改动与实际 base；后续验收范围为工作流、两个 Containerfile、打包/审计工具及对应文档，不同时修改 ABI、Qt/Vulkan 桥或模型求值。

1. 在可用 Docker/Podman 环境运行 [三套分发配方](github-actions.md)。维持 Ubuntu20 / Debian12 镜像及工具版本；Debian apt 使用冻结快照，focal 记录实际包版本。不换成最新 Ubuntu 来绕过低版本编译问题。
2. 原生程序与 KDE5 完成 x86_64 构建，KDE6 完成 x86_64/aarch64 构建。无相应环境时记未执行；若运行 GitHub 工作流，记录 run 链接、源码提交、日志与归档 hash，不推送/发布 release 或操作真实桌面，除非用户另行授权。
3. 原生程序通过 `ci_native_audit.py`；KDE5 包通过 `--baseline-ubuntu20`，KDE6 两份包通过 `--baseline-debian12`。保留实际 Qt 版本、ELF requirements、相对 RPATH、动态 libstdc++、依赖许可和 toolchain 清单。不得降低报告中的 Qt 版本骗过门槛。
4. 运行当前分发/部署正负例（本轮16项）、actionlint 与 bash 语法检查。兼容版本、缺库、错误架构、篡改 manifest 必须明确拒绝。归档在隔离目录解压，用随包工具预检/安装/verify，不修改真实用户配置。
5. 仅修复可复现的构建/打包问题，记录失败和修复前后；涉及公开 ABI 或 GPU 行为的调整先交 Codex 复核。构建成功不等于跨发行版 GPU 验收。

验收：三个工作流、四份归档的真实日志、兼容审计、归档 hash、负例与隔离安装均可复核；无法执行项单列。本机 Arch 包不满足基线，这是预期拒绝。首轮 Actions 日志已复现并修复。三个 x86_64 模式已在 Containerfile 指定镜像的完整隔离根环境执行同一 CI 脚本，191/193/192 项必选 CTest、严格基线审计和归档通过；见 [方法、修复与归档 hash](github-actions.md#修复后的隔离基线验证)。该环境使用 rootless user namespace / bubblewrap，不是 Docker 引擎或 GitHub runner；修复后的 Actions、KDE6 ARM64、隔离安装工具验收和真实桌面仍需补齐，不重复把本轮基线构建列为未执行。

## Gemini：V21 固定条件视觉检查

复用 C12 证据，不修改 GPU 同步/公式。固定模型、输出/DPI、seed/步数及同一资产来源；先检查 Chocola 的 GL/VK UI/背景和上下方向，再抽核 Fraise 与 Milk 的遮罩/触摸覆盖。数值比较使用既有容差，不以“看起来相近”放行。

在用户安排的 miku 会话中可核对 OpenGL 默认、Vulkan 选择、背景/完整设置面板、左键长按拖动、滚轮缩放、resize/暂停恢复与改回 GL。只用自身应用事件或用户操作，不向系统窗口注入输入，不自行注销/重启桌面。记录 GPU/驱动、X11/Wayland、实际 Qt 场景后端和每项证据；缺少共享图像扩展时验证错误提示与 GL 可恢复，不能记 Vulkan 成功。源码/固定图像与真实桌面分别统计。

## Codex 保留项

复核基线包与跨模块改动；Qt 非 OpenGL 场景图、意外 device/context loss、角色渐变预算和 CPU 配置保存失败事务仍未闭合。面板/FPS/声音收拢另需明确契约后再分派；不让 GLM 重新猜测同步或模型语义。C18 已验证嵌套0x7fc和离散mode1，见[合成语义](synthetic-semantics-2026-10-14.md)；C8 的 3 bust_LR20、2 标量继续保留，触摸候选不应用。

GPU 用 `build/gpu-validation.lock` 串行，冻结二进制/hash/输入；外部素材只读。每阶段验证后提交，填写真实 Agent/Model/Context；未执行、候选与 PASS 分开。

2026-10-13 保存补修：已核实当前桌面仍加载旧安装库；共享角色/UI 恢复及越界回退、60 FPS 默认和 Qt 配置加载边界已验证。构建 stage 已生成，尚未更新用户桌面；见 [本轮证据](layout-persistence-2026-10-13.md)。
