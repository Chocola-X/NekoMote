# Agent 协作任务

2026-10-13，C12 基线 `cfed43e`。Plasma OpenGL 初步接入已获用户确认；当前完成 Vulkan ABI 复核、Plasma 双后端和分发入口。详见 [显示补修](plasma-display-fix.md)、[任务交接](agent-next-handoff.md)。源码交付与完整独立验收分别计。

本轮 C13 以 `3a56ce0` 复核 GLM 未提交优化，实际结论见 [优化审查](optimization-audit-2026-10-12.md) 与 [验证](validation/optimization-results.json)。G34 优化候选契约见 [建议](optimization-suggestions-2026-10-12.md)；用户现已结束本轮优化，C17 Wayland 兼容已交付，当前任务为 C18 合成模型求值验证，后续优化不自动领用。原优化清单的“135 项已确认”和全量立即执行顺序不再有效。

| 范围 | 当前状态 | 下一步 |
| --- | --- | --- |
| D1 真实 Wayland / KDE6 桌面资源测量 | base `88ea8fe`；Debian12 当前构建 200 必选检查/重装校验通过；真实 KDE6/Wayland 双后端各 2×120 秒，静态前后/驻留另测，见 [报告](plasma-desktop-2026-10-18.md)；原图片背景和资产配置已恢复，保留新安装 | Vulkan 第二轮 RSS 较第一轮高约 69 MiB、切回静态后驻留未归因；实际 present/多屏/其他模型/长期仍未验证 |
| S1 系统依赖 / Arch 打包 | base `cffc7e4`；新增显式系统依赖模式和三包 PKGBUILD，固定模式/GitHub 默认保留；201 系统依赖 CPU/QML、199 固定依赖 CPU 通过，见 [说明](../packaging/arch/README.md) | 三份实际包审计通过，ARM64/干净 chroot 和其他发行版未验证 |
| R1 Release / layer-shell 分发 | base `8ef8309`；新增 Debian12 layer-shell x86_64/aarch64，七份包完整校验；移除 README、合并依赖声明并迁移 Plasma 安装；200 CPU CTest、29 定向 Python 及三套旧基线打包回归通过，见 [CI 说明](github-actions.md#layer-shell-分发与归档精简2026-10-18) | ARM64、远端 Actions/标签发布及目标桌面待实际运行；未做性能测量 |
| F1 共享 UI 字体回落 | base `842f7cc`；完整 CJK 字体优先、四语文本按需准备、候选重试/缺字回落与详细诊断；199 CPU、6 字体检查、GL/Vulkan 各2 GPU 用例及 player/layer-shell 构建通过；本机映射206→1 | 朋友 Arch / Hyprland 重测；限定内存/字符覆盖与 GPU 范围见 [记录](ui-font-fallback.md) |
| L1 layer-shell 壳 | base `98d0075`，Codex 实现及独立源码复核；独立 Wayland/EGL/OpenGL 高层 C ABI 宿主；192 CPU CTest、18 隔离客户端检查通过 | Sway/Hyprland/niri 实机、最终合成像素、真实输入、热插拔/长期待验收；独立分发见 R1；见 [说明](layer-shell.md) 和 [记录](validation/layer-shell-results.json) |
| C19 mesh.cc 调查 | base `94af01c`，Codex及独立只读复核；程序生成15格受控实验11正常捕获/4 NaN，PsBuild失败复现，114 Python用例（12 skipped） | 尚无在生效路径产生有限可辨识效果的非空布局，不放行公式；见[调查](mesh-cc-investigation-2026-10-14.md) |
| C18 合成模型求值 | base `693fd3f`，Codex 实现与独立只读复核；discretization1、嵌套0x7fc、sync/combine传播；51/51合成图/几何、4真实/2PsBuild、197 CPU与76 ASan runtime通过 | cc、其他离散/继承位、frame.type1及既有标量仍待验证；见 [报告](synthetic-semantics-2026-10-14.md) 与 [记录](validation/synthetic-semantics-results.json) |
| C17 Wayland/X11 兼容 | base `0c0f400`，Codex 实现及独立复核；GLFW 双平台、Qt GL/GLES、GPU 共享和 renderer 重建快照；189 CPU、3 GPU runner、8 对基线 / 10 对 GL/ES 画面、隔离 Qt/Plasma 通过 | 未安装用户桌面；真实桌面、原生 X11 登录及其他驱动仍待验收，GLES padding 显存代价见 [报告](wayland-compatibility-2026-10-13.md) 与 [记录](validation/wayland-compatibility-results.json) |
| C16 稳态帧优化 | 基线 `958a404`，Codex；索引/UV 和帧内图集/遮罩状态复用；195 初态、12 对画面、CPU 189/189、GL/VK 及隔离 Plasma 通过 | CPU probe P50 降低 2.0–4.6%，player 提交改善约 1.5–1.8%；显存不变、尾部及范围见 [说明](frame-optimization-2026-10-13.md) 与 [记录](validation/frame-optimization-results.json) |
| C15 纹理裁切 | 基线 `a17b5ad`，Codex；加载时 RGBA 精确边界 + 过滤边缘，GL/VK texel 采样，映射像素所有权；202 图集复原、12 对画面、异步切换及隔离 Plasma 通过 | 已构建验证，未安装桌面；首次驱动编译/其他驱动/长期范围见 [说明](texture-cropping-2026-10-12.md) 与 [记录](validation/texture-cropping-results.json) |
| C14 内存优化 | `a17b5ad` 已交付，流式模型加载、libpng、字体映射与生命周期测量 | 本轮 C15 接续，原范围见 [报告](memory-footprint-2026-10-12.md) |
| C12 Plasma / ABI | ABI 0.6.0；GPU 共享图像、设置选择、GL/VK 画面与隔离正常生命周期通过；帧率 ABI 查询已补入 | 真实 miku Vulkan、其他驱动与 Wayland 未验收，见 [说明](plasma-dual-backend.md) |
| G33 基线分发 | 用户确认 Actions 产物本体/KDE5/KDE6 正常；新增本体 ARM64 与 `vX.Y.Z` 自动发布，见 [CI 说明](github-actions.md) | 首次标签运行核查七份包、SHA256 和 Release；本体 ARM64 尚未实际编译，用户反馈不扩展为未提供的架构/长期覆盖 |
| V21 双后端桌面 | 隔离窗口/包证据通过；真实 miku 的 Vulkan 待用户测试 | Gemini 按受控条件核对角色/背景/UI，记录桌面/驱动与未覆盖项 |
| C11 Vulkan player | 从 `b733626` 重写；核心/双后端 GPU/真实图像/实际窗口与同步验证通过；GL 保留，同一二进制选择后端 | 当前 player 修复已交付；GLM 可补充其他设备构建与明确契约的批量测试，Gemini 做受控视觉抽核，Codex 复核差异；见 [后端说明](vulkan-backend.md)，C11 数量不替代 C12 的独立 Plasma 验收 |
| C9 Plasma 显示补修 | Codex 修复 UI 首帧/目标/输入/资源重建及重装偏好；隔离截图与 KPackage 检查通过 | 用户已确认实际桌面初步接入正常；后续完善服务与完整验收 |
| C10 左键手势 | 共享 Controller 负责短按释放触摸、长按 300ms 拖动和指针锚定缩放；ABI 0.4.0 携带采集时间，所有调用者同步；隔离验证通过 | 初步接入用户验收通过；逐项手势覆盖未提供，右键由桌面使用 |
| G31A/G31B/G32 | GLM 已交付共享 Controller/UI/GL 执行及高层 ABI/Plasma；完整边界尚有缺口 | 不重做已提取模块；后续优先 GLM 收拢 player/ABI 的面板业务与控件联动，由 Codex 给出范围复核 |
| G30 服务与时间 | 声音接入源码存在；Plasma FPS 调度已接线；设备控制及渐变暂停未最终验收 | 真实桌面核对 FPS 持久化/节奏；继续处理静音与暂停契约，不在壳复制业务 |
| C8 求值 | 单边界 0x7fc、195 初态沿用；嵌套0x7fc由C18闭合；3 bust_LR20 与2 标量未闭合 | Codex 继续语义反例；不扩大正式放行 |
| G27/G28/G29/V19 | 报告和提交存在，部分独立证据未复核 | 按真实覆盖抽核，不盲重跑或把报告当最终 PASS |
| V20/长期/全动态 | 实际桌面初步接入已获用户确认；完整生命周期、多屏、8–24h及 195 全动态未最终验收 | 各自记录条件；Gemini 补充固定条件下的逐项视觉与交互证据 |

用户已结束优化，C17 兼容已交付，本轮按用户授权推进 C18 合成模型语义；G33 基线构建与打包验收仍是独立待办；不猜测 Vulkan 同步与图像语义。GLM 其余可承担明确契约下的测试运行、证据清单和其他设备构建；不得因没有错误日志宣布 GPU 画面通过。Gemini 可按固定模型/步长/输出补充跨后端文字、遮罩与触摸覆盖的截图目视检查。当前没有要求 GLM/Gemini 并发修改本轮修复文件。接手先声明 base/文件，保护未提交工作；GPU 使用 `build/gpu-validation.lock`，输入仅经应用回调，外部素材只读。新证据目录冻结二进制/hash/输入/argv/截图，失败、未跑、候选与通过分开。触摸候选不应用；旧历史失败数量不代表当前加载状态。
