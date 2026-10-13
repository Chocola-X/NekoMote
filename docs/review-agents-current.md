# 当前整合与接手摘要

2026-10-12，C12 基线 `cfed43e`。共享 Controller/UI/高层 ABI/Plasma 已有实现，不重新执行旧 G31 待实现清单。用户此前确认 miku 用户真实桌面 OpenGL 初步接入正常；具体逐项覆盖未提供，Codex 未操作其桌面。

C13（基线 `3a56ce0`）复核 GLM 的未提交优化并落地局部改进、CPU 候选线程修复与死代码清理；当前优化入口为 [复核](optimization-audit-2026-10-12.md)，下一步 G34 按 [验收契约](optimization-suggestions-2026-10-12.md) 执行。批量建议不等于确认缺陷，不关闭运行时/ABI 帧检查。

C11 已修复 player 的 Vulkan 1.0 执行链。C12 复核 GLM 的 Vulkan ABI，修复目标借用/异常/资源替换与 C 宿主重复翻转；当前 ABI **0.6.0** 增加 Plasma 帧率查询。Plasma 设置可选 OpenGL/Vulkan，默认 OpenGL；Vulkan 经 GPU 共享图像接入宿主 OpenGL 场景图，无正式帧 CPU 读回。隔离图像、交互、切换和正常生命周期证据见 [Plasma 后端](plasma-dual-backend.md)、[ABI](abi-contract.md)。真实 miku Vulkan、其他驱动、Wayland、多屏与设备故障仍需各自验收。

GitHub Actions 的 x86_64/aarch64 低版本基线配方与包审计已准备；本机包实际要求 Qt 6.9/glibc 2.38，不能作为通用包。容器、GitHub 远端执行及跨发行版运行未验证，见 [分发](plasma-distribution.md)。

G31/G32 的完整架构验收仍有缺口：player 和 ABI 组合层重复面板业务，静音设备联动、角色渐变暂停预算及 CPU 配置保存失败事务未收拢。共享配置现在保存帧率、角色变换和控制面板位置；Plasma 更新节奏跟随面板帧率。application/ui 保持后端无关；适配器经 C ABI 复用共享实现，不在 QML 重写控制面板。

单边界 0x7fc/195 初态沿用；3 bust_LR20、嵌套/2 标量、触摸候选、195 全动态和长期声音未最终验收。本轮不更改模型公式。有效任务见 [清单](agent-workboard.md)、[交接](agent-next-handoff.md)、[缺口](known-defects.md)；历史报告只描述当期证据。

2026-10-13 保存补修：已核实当前桌面仍加载旧安装库；共享角色/UI 恢复及越界回退、60 FPS 默认和 Qt 配置加载边界已验证。构建 stage 已生成，尚未更新用户桌面；见 [本轮证据](layout-persistence-2026-10-13.md)。
