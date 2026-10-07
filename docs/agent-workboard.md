# Agent 协作与下一批任务

基线：`ccc5745`；依据 [2026-10-07 审查](review-2026-10-07.md)。这是可交接清单，不表示已启动其他 Agent 或任务已完成。状态由执行者记录到 development-log，验收后再更新 ROADMAP。

## 分工原则

用户希望优先使用有无限额度的 CodeArts / GLM-5.2，视觉工作交给 Antigravity / Gemini 3.8 Flash。下面是本项目任务安排，不是模型能力排行榜：没有统一能力评测或当前工具列表，不能仅凭模型名断言支持/不支持某能力。Agent接手时先确认实际工具；缺视觉读取就准备证据包交接，不能声称看过截图。

| Agent / 模型 | 优先任务 | 需要另一执行者复核的边界 |
| --- | --- | --- |
| CodeArts / GLM-5.2 | 批量资产/证据采集、显式路径与构建工具、共同契约已明确的实现和负例测试、ABI错误码/长度/RAII、格式和文档同步 | 不能从“运行成功”推出画面正确；未知求值公式、遮罩来源、GPU混合、浮点顺序需数值及视觉复核；并发/ABI所有权设计先评审 |
| Antigravity / Gemini 3.8 Flash | 原生/WebGL并列图与局部差异定位，真实遮罩/手部轮廓、UI排版/拖动/缩放/捕获状态，产生可复现的视觉报告 | 视觉改善不能放宽误差门槛；不能单凭字段名/图层名推导求值公式；涉及runtime/GL生命周期改动需契约和独立数值审查 |
| Codex / 本会话实际模型 | 求值语义推导与架构整合、ABI/线程/生命周期审查、跨模块依赖和测量方法、冻结门槛与最终阶段验收 | 同样必须提供实测证据；本次未获知的精确模型变体不猜写，提交记录使用会话提供的实际信息 |

尽量将明确、可自动验收的工作先交GLM-5.2；Gemini集中处理需要读图的任务；Codex处理跨模块或语义不确定性。资源额度决定调度优先级，不降低任何Agent的交付标准，也不要求进行无意义重复测试。

## 视觉清单：交给 Gemini 3.8 Flash

每项交付包括：模型/输入/步数/seed/资产和源码hash，参照与原生PNG、差异图和区域裁图，标量/几何/alpha/黑白合成报告，严格PASS/FAIL，已观察现象与候选原因分别记录。来源数据只能用于对照，不能写回原生正式帧。先做V1，再做V2；V3–V5可在没有核心修改时分别开展。

- [x] **V1 / R1：Fraise已通过样例的遮罩回归，最高优先。**
  - 输入：`docs/validation/playback-blink-inputs.json` 的 `fraise-seed1-long400`，固定seed1、400作者步、待机差分。
  - 根因：`src/runtime/geometry_scene.cpp` 中无条件 `if (name == "add_mask") continue;` 错误跳过了 `fraise-casual-a` 等模型中真实存在的 `add_mask` 图层节点，导致遮罩漏切眼缘（超差 165 像素）。
  - 修复：移除字符串硬编码跳过，恢复真实图层作为遮罩几何源的解析。当图层存在时正常加入 `roots`；当源图层缺失时（如 `fraise-b` 系列尚未解析的嵌套来源）保持严格拒绝（`UnsupportedMask`），不再虚假放行。
  - 验证证据：
    - `fraise-seed1-long400`：超差像素由 165 降为 **0**，原生 PNG sha256 为 `f1a258aba1c734cd004dff35875ff6ba5050b3b6ca5d98600f6d4007d426342a`，最大 RGBA 误差 1/255，黑白背景合成误差均为 0；
    - `playback-blink-inputs.json` (blink11)：**11/11 Passed**，全部 0 超差像素；
    - `playback-eye-inputs.json` (Eye10)：**10/10 Passed**，全部 0 超差像素；
    - `playback-fraise-inputs.json` (Fraise3)：**3/3 Passed**，全部 0 超差像素；
    - `static-inputs.json` (static30)：**30/30 Passed**，全部 0 超差像素；
    - CTest 单元测试：**133/133 Passed**（新增真实图层解析及缺失时严格拒绝的双向测试）。
- [ ] **V2 / R2：Coconut手部与局部继承。**
  - 当前复现：coconut-maid，body_LR=30，零步，scale=.45，origin=(0,100)，800×1080，透明背景、SDK物理scale0；几何8.876191px、13,111超差像素。
  - 范围：coconut-maid、maid-a、maid-a-4yo、santa、wintermaid 及 catalog 中全部 0x7fc 型号。
  - 视觉交付已完成（详见 [docs/coconut-hand-local-inheritance-report.md](coconut-hand-local-inheritance-report.md)）：
    - 输出左右手高分辨率裁切对照图与差异二值图（`build/evidence/v2_analysis/`）；
    - 量化确认：其余身体节点误差全部小于 0.000106 px，误差 100% 集中在 0x7fc 挂接点；
    - 关键顶点抽样：Y 坐标完全重合（0.000000 px 误差），X 坐标偏移精准为 +8.876129 px；
    - 几何数据与挂接矩阵已整理，待 Codex 进行最终数值公式闭合或恢复严格拒绝。
- [ ] **V3：混合、attr、mirror与透明合成。**
  - GLM提供按字段筛选的最小型号/节点清单；对bm=16/19、attr=2等做源GPU协议观察和真实图像，不按数字猜blend。
  - 同输入分别在透明、黑、白及指定真实背景比较；检查翻转后的遮罩裁剪、触摸/视线坐标和global alpha端点/中间值。
  - renderer新增alpha/flip尚无针对性GPU回归；与现有保护透明色模式交叉验证。
- [ ] **V4：角色/背景切换与生命周期画面。**
  - 固定作者时间、0/.1/.2/.4s角色过渡、0/.4/.8s背景过渡，含重入、暂停/恢复、reset、resize及加载失败。
  - 检查重叠区域是否符合约定的cross-fade，背景alpha/旧角色残影/位置跳变；先写出期望行为再比较。
  - 通过应用内诊断调用触发；不得xdotool或系统键鼠注入，不切换实际桌面配置。
- [ ] **V5：控制UI可见状态与输入隔离。**
  - 收起/展开、角色/设置抽屉、打开关闭中途反向、图标及面板拖动、视口边界、HiDPI与不同窗口尺寸、声音/锁定/帧率状态。
  - 比较风格和排版时记录字体/字号/device scale；网页与原生UI不同排版/抗锯齿不应伪称逐像素相同。模型渲染阈值和UI布局/行为验收分开定义。
  - 面板上点击/拖动/滚轮不触发模型；面板外视线和触摸正常。补实际状态机回归，不能引用现有128项当UI测试。

V2最小复现命令（先用显式外部素材导入到assets；输出目录自行选择新的名称，保留旧FAIL）：

```sh
python3 tools/reference_capture/capture.py --reference-root ../NekoWebShow \
  --browser /opt/chrome-linux/chrome --model coconut-maid \
  --output build/evidence/v2/coconut-body30-reference \
  --scale 0.45 --x 0 --y 100 --variables '{"body_LR":30}' \
  --physics-scales '{"hair":0,"parts":0,"bust":0}' --steps 0 --trace
build/dev/bin/nekomote_preview --geometry-diagnostic \
  --asset assets/characters/coconut-maid/model.nkm.json --origin 400 640 --scale 0.45 \
  --variable body_LR 30 --hidden --screenshot build/evidence/v2/coconut-body30-native.png
build/dev/bin/nekomote_pose --asset assets/characters/coconut-maid/model.nkm.json \
  --variables '{"body_LR":30}' --sample-trace build/evidence/v2/coconut-body30-reference.trace.json \
  --output build/evidence/v2/coconut-geometry.json
python3 tools/reference_capture/compare_images.py \
  build/evidence/v2/coconut-body30-reference.png build/evidence/v2/coconut-body30-native.png \
  --output build/evidence/v2/coconut-body30-comparison --tolerance 2
```

浏览器路径是此次诊断的显式参数，不是正式构建/运行依赖；换电脑时选择实际可用浏览器。pose从trace对应metadata取得原点/比例，不额外传不存在的`--origin`选项。

## 优先给 GLM-5.2 的清单

以下G任务的契约来自审查记录。涉及公共ABI/服务重构时，先提交接口草案和负例表供Codex复核，随后自主实现、迁移调用者、测试；不是每个普通编辑都重新向用户确认。

- [ ] **G1 / R3–R4：ABI可观测失败与RAII。** 明确renderer各失败返回值/错误来源及失败后的帧有效性，所有wrapper部分初始化用RAII，长度/溢出/枚举/空指针负例可重复测试。完成真实C数据布局边界；借用寿命/销毁顺序必须写入header。CPU ABI测试从图形开关及本地真素材依赖中解耦，GL错误另设GPU测试。验收含非法enum999拒绝、无效loader不泄漏、失败不能继续画旧帧。
- [ ] **G2 / R5：精简C验证宿主。** 不再复制WAVE/Pulse/RMS服务；先列服务边界和线程/队列/失败契约，复用现有audio后测试pause、stop、重播、满队列、设备失败和候选模型切换。禁止把活pthread对象浅拷贝/清零；无共享锁跨设备阻塞调用。初期不要另造一套C宿主壁纸应用。
- [ ] **G3 / R6：application时钟与能力负例。** 修复触摸能力对全部区域的检查，建立背景暂停/大delta/reset/重入的显式契约，补时间分区及零分配测试；新旧角色候选加载失败保留当前会话。数值问题交Codex，画面交V4复核。共同策略确定后再同步preview和C宿主。
- [ ] **G4：可移植资产与目录账本。** 修改现有工具接受reference-root/importer/output；优先复用`tools/import_emote/audit_catalog.py`已有的catalog/alias映射，不新增按os.listdir首文件猜选。事务导入、失败非零退出、已有partial/旧schema明确重建。195目录每项保留结构/CPU/视觉/交互/稳定性独立状态，milk-winter等缺项仍占一行；磁盘目录200不替代产品目录195。清理已跟踪资产只移出索引、保留本地文件，不改历史或外部素材。素材来源修复记录hash和方法。
- [ ] **G5：可信性能工具。** 停止改写原仓库；使用只读服务或隔离临时副本。两端明确就绪/预热完成/测量开始结束握手，固定模型、动作、seed、背景、声音、UI、输出和实际FPS；fail/timeout/error不得生成成功报告。进程CPU、PSS/共享RSS、整卡GPU/显存、实际呈现分开，重复串行测量并记录驱动/时钟/功耗。先撤回未控制条件的收益结论，再输出可复核原始数据。验收前无需追求优化数量。
- [ ] **G6：构建、格式和依赖矩阵。** 按.clang-format整理新ABI/C宿主/application代码；修正C/C++语言专属warning和target依赖，移除项目代码-Wno-error/截断掩盖；Fontconfig实际必需就明确find_package并登记许可/版本。验证clean checkout无assets、headless、player关闭、图形宿主配置、ASan CPU ABI。缺真素材测试明确Skipped并给显式集成入口，核心负例不能Skipped。
- [ ] **G7：证据整理，适合批量工作。** 针对V1–V5准备源节点路径、参数范围、source/input/binary hash、截图/trace对照索引；导出mask命令不能只导主几何。现有原始数字/FAIL不覆盖；图像由Gemini读取并确认，GLM没有读图能力时只汇总机器结果。
- [ ] **G8：UI绘制成本与真实状态测试。** 根据Gemini给出的V5缺陷逐项实现，补无窗口状态机测试、加载阶段字形准备及绘制分配测量；不按每帧glyph创建/纹理上传。数据驱动UI目录，动作/反应/设备策略保持在共同模块。尺寸/性能的改变均需V5截图回归。

## Codex整合与阶段门槛

- [ ] 审核G1/G2边界，推导R2挂接点公式及V3混合语义；恢复严格拒绝未验证能力。
- [ ] R1/R2闭合后重跑115组冻结矩阵；本次Chocola4通过不代替全量回归。重新导入造成hash变化时记录合法schema变化和输入版本，不修改参照hash或阈值使其通过。
- [ ] 完成共同会话/设备/错误契约后，读取实际Plasma/Qt Quick图形后端，建立小型原生宿主GPU验证，再开发WallpaperItem/QML。验证失败不改全局桌面后端、不采用CPU像素桥接。
- [ ] 可安装产物和实际桌面/长期验收分开：不得在8–24小时未运行时认证P9，其他桌面留到已验收范围之后。

## 共用工作区操作与交接

1. 开始前记录base commit、Git状态、任务ID、拟改文件及依赖；在development-log注明领用。一个高风险文件同一时间只由一个执行者修改。可以用隔离worktree开展候选实验；共享目录时明确文件所有者，不能重置别人的未提交工作。
2. GLM负责批量检查与具体修复，Gemini负责有输入的视觉检查，Codex负责跨模块整合；可交换角色，但先证明所需工具/验证能力。角色安排不是用户权限授权或对外发布授权。
3. 相依任务顺序执行；独立证据可同时采集，但GPU正确性/性能测量串行、不能与构建替换正在使用的二进制竞态。长任务冻结二进制并记录hash，逐case保存检查点。
4. 每个阶段输出：修改内容、实际命令、PASS/FAIL/Skipped、图片hash、性能范围、剩余问题、下一任务ID。提交正文含Agent、Model、Context；集成者记录被整合的提交，不重写来源信息。
5. 即使截图改善/全部现有单测通过，只要必需语义仍未验证，就保留明确不支持及失败证据。禁止用文档把现有偏离改写成新的正式目标。
