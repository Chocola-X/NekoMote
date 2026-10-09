# 下一批独立任务

基线：最新正式继承整合提交，见[本轮复核](review-g25-integration.md)。用户要求GLM承担实现、批量运行和证据整理大头，Codex接管求值公式并做生命周期复核；Gemini实际看图。先声明base/文件，保护共享工作；GPU用build/gpu-validation.lock，原素材只读，输入只用自身回调。每阶段验证后commit，真实Agent/Model/Context，报告≤一页，raw放新build目录。

## 已完成的继承阶段

G25静态部分与V17静态观察已复核；Codex补了其余12型号24静态参考、三代表idle120及正式195/核心/GPU/ABI/player回归，单边界0x7fc现已正式整合。见[整合记录](review-g25-integration.md)。不再从d297827建候选或重跑旧27矩阵；嵌套仍拒绝，45组合和全动态未认证。G25报告并不代表G22/G24已执行。

## GLM G22：实际Plasma包剩余功能与部署（已完成）

G19的初态重建/配置页真实创建/首delta0/40轮旧renderer及最终销毁已由Codex修，不重复推倒；见[g19](g19-plasma-report.md)。包probe是预链接native的实际KDE宿主，不能据此宣布干净动态部署已验证。

本轮完成（见[g22报告](g22-plasma-report.md)）：
1. `--recovery`模式：bad→good→bad→good恢复，model/session/originY/scale切换，旧state/GPU销毁，动作继续推进；非local URL/缺配置/非法scale/origin分别到达对应分支；批量更新只重建一次。
2. CTest `plasma_package_probe`：6测试（bad-asset/invalid-config/capture/lifecycle/recovery/dpi1.5），9默认+1可选计数，每轮诊断隔离，首delta0信号，resize containment/FBO断言，DPI 1/1.5，CPU peak RSS与renderer计数分开。
3. CMake target `nekomote_plasma_stage`/`_clean`：自包含stage（native库/plugin/qmldir/qmltypes/KPackage），`tools/plasma_stage_verify.py`验证+manifest。plugin RUNPATH指向build（patchelf不可用），运行时需LD_LIBRARY_PATH覆盖。
4. `tools/plasma_install.sh --install/--uninstall/--status`：用户XDG安装，不装系统/不改桌面/不重启shell/不复制PCM。

修改：wallpaper_item.cpp setter静默更新+configure_diagnostics分离输入隔离与固定步长；package_main.cpp添加recovery+FBO断言+每轮诊断隔离。141单元/37694断言+9配置+4安全+16恢复+20生命周期全通过。长期/多屏/真实桌面安装/正式声音接入未验证。

## GLM G24 → Gemini V15：真正可消费的触摸视觉证据

G18/G21最终81点有效，不重复写validator。G21汇总有2个旧失败但修正最终42OK；来源文件路径及缺freeze见[g21](g21-touch-handoff.md)，保留失败历史，用新准确索引。

1. 保存直接读外部pixelMarker/presentation的生成入口、source/asset/tool hash与逐字段比较，不复制session/G13区域充当来源。修正42点汇总并绑定实际request/response/hash；当前候选保持PENDING。
2. 为Fraise B/Cinnamon maid-b/Coconut santa-b准备default、idle120、moved、half及真正下身进入画幅的Qt/FBO clean/overlay图。实际FBO尺寸、PNG尺寸/alpha bbox一致，排除旧GLFW2160→1600裁切；覆盖图只用真实区域投影，不自动生成观察。诊断布局独立保存，不覆盖G13冻结布局。
3. 各条件图、原probe、steps/seed/origin/scale/mirror、来源参考、点位request/response/PNG绑定并冻结。新诊断条件不能假冒G13条件；若现有观察工具只接受冻结G13，先建立明确的新捕获manifest入口/对应契约测试，不能移除来源检查或凭hash接纳任意图。新增条件由Codex抽核证据边界。

Gemini V15随后逐图实际观察，填写工具支持的version3（version/observations/observer、逐condition角色/path/hash/reference）；缺图/被遮挡/画幅外/未检查区分。旧lower_body_diag下身UNCHECKED，撤回“网格cutoff”解释。不硬编码MATCH或按家族外推，不根据椭圆数学认证候选，不改素材/工具/公式。视觉任务只交一页docs/v15-touch-report.md及实际证据。

## Gemini V16：Plasma视觉复核

可先看Codex本轮实际包40轮最终图与V13有效裁图，G22后逐条件核对配置/错误恢复/resize/DPI/输入。区分实际包与独立QML三固定图；常态不同时间PNG不用于固定像素判定。实际执行命令、图/hash冻结，不凭同一静态图宣称播放/长期/无泄漏。

## GLM G26：剩余求值验收与实际模型分配（G22/G24之后）

从最新主分支开始，公式已有正式实现，不独立改 `src/runtime/geometry_scene.cpp`。旧G25 45条件仅加载，继续补真正参照；原始输出不改写，缺项列机器可读NOT_RUN/SKIPPED。

1. 建立可提交、相对路径/CLI配置的采样入口，从G25实际variables/request恢复45组合并生成原WebGL+正式native几何/图；不得只用全图bbox判眼/口/胸。沿用fixedClock1000/seed1、作者步1、同origin/scale/初态，目标几何≤.001px、物理标量≤1e-5、顶点/三角形完整、alpha/黑白合成RGB≤2且零超差。body_slant字段实际核验。SDK GetVariable主通道不含差分，最终混合用真实几何/图验证。
2. 扩动作/过渡、组合差分、中途事件、暂停恢复与locator/触摸投影。先用三个代表，同实例固定步序列；源码/资产/原PSB/工具/二进制/输入/输出hash及每条argv/stdout/stderr冻结。已有三代表idle120终态及120步物理通道无需重复认证；这不覆盖其他时间点/动作/恢复。
3. 真实模型evaluate/build_frame稳态分配计数，分别报告application/renderer/UI覆盖；可复用容量但不得在正式热路径加计数原子/读回。P50/P95/P99附真实CPU/驱动/模型/动作/分辨率/预热/样本/构建。合成零分配和CPU pose计时不能替代实际应用。

现有入口：`tools/reference_capture/capture.py`、`compare_images.py`、`compare_playback_frame.py`、`validate_playback.py`；正式工具 `build/release/bin/nekomote_playback_probe`、`nekomote_pose`、`nekomote_preview`。Chromium已实跑：`/home/chocola/.cache/ms-playwright/chromium-1194/chrome-linux/chrome`，Playwright依赖在tools/reference_capture/node_modules；其他机器用参数传路径，不硬编码本机。完整可复算示例在 `build/evidence/codex-g25-integration/verify_dynamic.py` 与 `verify_static.py`，可整理成正式工具，不复制成运行时依赖。

任一公式/几何反例：保存最小request、字段路径、差值及图/trace，登记“等待Codex（仅该条件）”，不猜常数/改素材/降阈值；继续无依赖条件和G22/G24。工具启动失败先保存命令及stderr并排查路径，不把未尝试写作工具缺失，不要求用户替Agent转述整项任务。报告≤一页 `docs/g26-runtime-validation.md`；批量机械工作由GLM承担，Codex处理公式反例及复核。

## Gemini V18：新增型号与真实动态观察

可立即看本轮 `build/evidence/codex-g25-integration/new-models/` 的24组和dynamic三终态，再消费G26同实例多步/动作截图。逐条件实际看腕部/遮罩/局部变化，记录图/hash/request与观察，终态静态观察不写成动态连续性。旧V17条带排序错误，仅作历史证据；要重拼须新目录、明确参数标签，不覆写冻结图。只核图，不改公式/素材。触摸V15继续待G24包；Plasma V16可随G22实际包推进。

## 执行顺序与阻塞规则

**GLM G22→G24→G26**。G22/G24未做，优先完成；遇到核心求值问题只挂起对应条件，保存反例给Codex，继续独立工作。允许自己组织无共享文件冲突的子任务，但GPU串行；不得以部分采样交付宣称整张清单完成。每个完成/未跑/失败/待Codex项单列。

Codex负责求值公式/反例、架构/线程生命周期及最终整合；Gemini负责实际视觉。嵌套/2标量反例、正式声音背景、UI/渐变/reset、195完整动态、8–24h/多屏/安装仍在路线图；本轮只有明确范围的继承放行。
