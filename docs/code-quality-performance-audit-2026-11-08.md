# 2026-11-08 性能建议与 A 阶段独立复核

复核对象：`c86f469e2f39f53c2c787eab8b1e7dc143a0b956` 加当前未提交改动，原报告为
`docs/CODE_QUALITY_PERFORMANCE_REVIEW-2026-11-8.md`。本次只增加复核文档及诊断证据，
未修改其他 Agent 的实现，也未提交、安装插件或更改用户桌面。

后续处理：用户已授权修复和提交；最终改动及验证见[修复验收](code-quality-performance-fixes-2026-11-08.md)。
以下保留修复前的独立核查结论和证据。

结论：重复编译和重复 prepare 的优化成立，但 A 阶段不能按当前实现验收。
音频失败后的配置未传入异步加载器；暂停调度有交互延迟和 UI 失效错误；revision 缺少生产者身份。
后续建议的收益排序也应按真实资产调整，尤其不能将离屏目标的理论容量当成能够直接省下的显存。

## 1. 需要修复的问题

### R1 / P1：不同实例的 revision 冲突，renderer 静默复用错误几何

位置：`src/runtime/geometry_scene.cpp:746,1376`，
`src/render_gl/renderer.cpp:508`，`src/render_vk/renderer.cpp:635`。

每个 `GeometryInstance` 的计数器均从 0 开始，renderer 却只比较整数 revision，
没有比较生产者。同一个 renderer 依次接收两个独立实例的第一个帧时，第二帧被跳过。
`chocola-date-a` 两个实例的首顶点分别为 x=73 和 x=173，revision 都为 1；
第二次 prepare 后，从 GPU buffer 读回的 x 仍为 73。wintermaid 和 yukata 同样复现。

当前高层角色切换创建独立 renderer，因此这不等于已证明 Plasma 切换会出现此错误；
但公共 C++ renderer 没有“一生只绑定一个实例”的契约，新增协议本身不成立。
GL/Vulkan 的门控实现相同；本次 GPU 复现只执行了 GL。

应缓存“生产者身份 + 内容版本”，或采用具有明确唯一性与生命周期的内容 token。
仅用 span 地址也需处理对象销毁后的地址复用。补充两个真实独立生产者共用 renderer 的回归；
当前测试只验证单个手写 revision，无法发现此问题。

### R2 / P2：音频失败后，异步切换仍会解码整套 bank

位置：`src/abi/wallpaper_instance.cpp:466,478,680`；
`apps/player/main.cpp:918`；`src/application/controller.cpp:166,378`。

壳修改的是自己的 `controller_cfg_` / `controller_cfg`；Controller 在 `load_initial()`
时复制配置，`request_switch_character()` 捕获的是内部 `controller_config_`。
本次没有更新这个副本。结果是首次 bank 可以被释放，后续后台候选仍按 Device 模式解码，
也不能保证显式取消静音后后台加载重新启用音频。

探针按相同配置复制顺序执行：初始 Device，外部配置改 Disabled，然后异步切换；
候选仍含 19 条音频、6,177,912 字节 PCM。该探针验证配置传播问题，不连接用户音频设备。

应为 Controller 提供明确的音频加载策略更新入口，并统一当前实例、候选加载与设备状态。
不要为了更新音频而重新调用会重设 origin/scale 基准的整个初始化配置入口。
正在加载的候选也应有明确的取消或提交时处置策略。

### R3 / P2：X11 / layer-shell 输入不能绕过 500ms 暂停截止时间

位置：`adapters/x11/host.cpp:741,772,808`；
`adapters/layer_shell/host.cpp:1004,1059,1137`。

暂停后的 `next_frame_` 为 500ms 后。输入和 resize 虽置脏，却未提前该截止时间；
主循环仍要求 `now >= next_frame_`。因此恢复、拖动面板、悬停及 resize 的落屏可延迟近 500ms。
即使 `needs_render` 一直为真，暂停状态下也只有约 2 FPS。

应区分异步加载的低频轮询截止时间和可见更新的呈现截止时间，输入/输出变化及时调度；
UI 动画期间保持合适绘制频率。此项是调度代码的确定性推导，未在真实桌面测量输入延迟。

### R4 / P2：共享 UI 的脏标记既无法静止，又会过早停止关闭动画

位置：`src/abi/wallpaper_instance.cpp:836,851,995`。

`render_frame()` 清脏后，`render_ui()` 只要 sidebar.open 就重新置脏，
完全展开的静止面板也无法停止绘制。探针先展开面板再暂停，连续三次
advance/render_frame/render_ui 后 `needs_render` 均为 1。

反过来，关闭面板后 open=false，剩余关闭动画没有保持脏状态，下一轮可被直接跳过，
留下未完成的侧栏。探针关闭后立即得到 needs_render=0。
另外，暂停时传给 draw_ui 的 dt=0 被现有布局代码夹为 0.0001，
配合 2 FPS 会令动画极慢；这一 dt 行为原本就存在，本次调度进一步放大了后果。

脏状态应反映绘制后的实际视觉变化、未完成的动画及 UI 发出的业务操作，
在整帧成功完成后统一确认，不能以“面板打开”等价于“持续动画”。

### R5 / P2：player 暂停时点击面板“重置位置”后可能不更新角色

位置：`apps/player/main.cpp:1276,1378,1611`；`src/ui/menu.cpp:363`。

角色先绘制，随后 build_sidebar 才处理“重置位置”并修改 Controller；
帧尾又将 presentation_dirty 完全替换为“侧栏动画是否处于中间值”。
侧栏完全展开、角色暂停时，重置后的新几何没有被绘制，下一轮却因 dirty=false 跳过。
直到再次输入才可能落屏。此项为代码路径复核，未通过 GLFW 可见窗口执行交互回归。

应将 UI 引发的角色/背景/配置变更纳入共享视觉失效机制，避免 player 再维护一套不完整规则。

附带检查：六个 tools 文件仅发生 100755→100644 的权限变化，与本次性能优化无关。
显式用 bash/python3 调用仍可工作，但直接执行会失败；提交前应确认并移除无意权限变化。

## 2. 收益与优先级复核

| 建议 | 本次证据与收益边界 | 判断 |
| --- | --- | --- |
| revision 门控 | 三模型相同内容完整 GL prepare 的 P50 为约 17–22µs，命中缓存约 0.03µs；每帧避免 78,000–111,920 字节 VBO/EBO 上传 | 成立，先修身份冲突；不减少正常变化帧求值/绘制或显存 |
| 暂停停止呈现 | 稳定静止时确实可省去 GPU 提交；player 仍约 20Hz 轮询，两壳约 2Hz 轮询 | 优先修完；当前不是完整的事件唤醒，实际 CPU/GPU/功耗降幅未测 |
| 音频按需解码 | 本地 195 bank，PCM 最小/中位/最大 3.70/5.78/6.29 MiB；素材共享不等于不同会话共享 PCM | 有收益，但当前素材规模不足以支持“最大的内存优化”判断；先修 R2，再决定是否值得引入首播调度/缓存复杂度 |
| direct-over + lazy color target | 195/195 初态都有颜色保护，4 个还有 Multiply；按报告条件可直绘者为 0。4K 的约 31.64 MiB color + 4 MiB mask 是容量，不是已可回收收益 | 降低优先级，不能直接实现为默认快路径 |
| GL UI 合批 | 每图元上传/调用确实存在；面板展开时更可能受益，关闭后只有小图标；Vulkan 已聚合 | 合理候选，先测面板场景驱动调用/CPU 占比，再做相邻保序合批 |
| GLES padding | 三模型裁切后 atlas 的 RGBA 容量约 21.69–32.49 MiB，padding 后 32–64 MiB，潜在差值约 7.36–31.51 MiB | 对实际 GLES 宿主可能比按需声音更值得研究；desktop rectangle / Vulkan 无此收益，保持既有像素门槛 |
| geometry 编译复用 | 三模型内存中会话编译，消除重复 geometry 后 P50 少约 1.1–1.3ms；不含磁盘、解码、上传和设备初始化 | 已实现的转移所有权方式合理；只改善加载/切换，不应宣称稳态改善 |
| Vulkan 批量上传/子分配 | 逐图上传等待和逐资源分配属实；此次三模型各只有少量图集，未测 Vulkan 加载瓶颈 | 批量 staging 可作为后续候选；无 profile 不引入完整分配器 |
| 音频空闲唤醒 | 4/12ms 轮询属实，实际还取决于设备是否已创建、是否有 stream | 候选成立；降低唤醒有利空闲，但需保持命令/退出可靠性，本次未量化 |
| GL alpha 校验 | 与 VK 对齐，检查有限性及 [0,1]，格式检查通过 | 可保留；属于正确性修正，没有可宣称的性能收益 |

在正常 30/60 FPS 且每帧都有 author tick 的动画中，revision 门控通常没有命中收益。
若 120 FPS 中恰有 60 帧/秒重复，这组三模型微基准外推只省约 1.0–1.3ms CPU/秒
（单核约 0.10–0.13%）和 4.46–6.40 MiB/秒上传；这是局部推算，非整机实测。
因此该优化值得保留，但不应预期它单独带来明显的正常播放资源下降。

直绘还存在独立的语义反例：无 mask、无颜色保护、无 Multiply、view.alpha=1，
只画一个 50% alpha 白色四边形。在透明目标上，现有 PremultipliedOver 与
ProtectedStraight 的中心像素的导出 straight RGBA 分别为 (255,255,255,128) 和 (128,128,128,128)。
原因是现有 straight 累积和最终 blit 包含额外颜色/alpha 运算；普通 Over 的数学直觉不足以替代现有协议。
报告要求像素对照是正确的，但其列出的候选筛选条件本身并不保证等价。

建议顺序：先修 R1–R5，保留已确认的编译复用与 alpha 校验；在用户实际使用的宿主上
测暂停收益；随后分别调查 GLES atlas 和展开面板 UI。声音缓存、Vulkan 上传及离屏重构
按实际瓶颈再立项，不直接照原先 1→2→3→4 排序实施。

## 3. 验证方法与限制

环境：Intel Xeon E5-1680 v4，GCC 15.2.1，Release + LTO + Werror；
Mesa llvmpipe LLVM 20.1.8，surfaceless EGL / desktop GL。
`cmake --build --preset release -j 6` 成功；额外启用 X11 / layer-shell 后，
两个壳的 15 个生成/编译/链接步骤也全部成功（未运行真实宿主）；`git diff --check` 通过，
18 个变更 C/C++ 文件的 clang-format 检查通过。

每模型会话微基准预热 5 次、采样 40 次；prepare 预热 5 次、采样 1000 次。
完整 prepare 使用同一真实帧的 revision=0，复用路径使用非零 revision。
旧会话成本是在同一新版二进制中额外编译并销毁一次 geometry 后执行 compile_session，
模拟被移除的重复工作，**不是两个 Git 版本的端到端 A/B**。
模型默认静态初态，不含动作/交互采样；像素/顶点读回仅用于诊断，不在计时区间。
prepare 计时不交错 draw，不能反映实际在途绘制造成的 buffer 同步代价。
顺序采样、短窗口且无绑核，P95/P99 仅作本次样本记录，不是冻结预算。

软件 GL 的微基准不能代表真实 NVIDIA/AMD/Intel 驱动、Vulkan、GPU 显存或功耗。
未执行真实桌面暂停 60 秒采样、全动态、声音播放、长期或多屏验收。
当前构建只有 release，未缓存 Catch2 源码，故未重新运行原报告的 258 项测试；
其“258/258、10 skipped”只作为原作者声明，本次不背书为独立执行结果。
独立执行的是上述定向探针及 195 初态清单，不将资产检查数量称作单元测试通过数。

证据与复现命令见 `docs/validation/code-quality-2026-11-08/README.md`；
源码哈希记录的是接手时 24 个已修改跟踪文件，用于区分其他 Agent 后续修改。
