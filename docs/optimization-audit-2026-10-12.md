# 优化审查复核（2026-10-12）

基线 `3a56ce0`，包含 GLM 未提交的五个源码文件。原报告列举的 135 项是调查线索，不是已验证缺陷数量；行号只描述审查时源码。Codex 根据实际调用者、资源/线程契约与验证结果复核。原始报告和输入 patch 保存在 `build/evidence/optimization-20261012/`。本文件是当前执行结论。

## 已成立并处理

| 原编号 | 结论与实现 |
| --- | --- |
| Q17 | Qt 诊断 CPU 数值恒零。GLM 的 `std::clock` 统计整个进程，改为渲染线程 CPU 时钟，并明确 submission wall time 不是 GPU 时间；时钟失败报告 -1。 |
| Q18/Q19 | 配置探测创建空文件、HOME 未判空均成立。保留判空；改用有效用户权限探测，检查已有配置是普通文件。仅 owner_write 不覆盖组/ACL、执行权限、文件权限和只读挂载。权限检查不能保证稍后的保存一定成功。 |
| P5/P6/P20 | 零角度、每 primitive UV 倒数、整数幂可减少计算；保留零角度的 signed zero。浮点结合和舍入可能不同，须图像回归，不称零风险。无调用的 cubic easing 直接删除。 |
| P14/P16/P26 | 文本一次 reserve 并复用容量、缓存字体 ascent、加载时缓存 UI uniform；没有改变透明图层绘制顺序。原 vector 已按倍增摊销且预热后复用，不能声称旧实现每字符都分配。 |
| P31/P34 | Vulkan 帧缓冲显式持久 coherent 映射；纹理两次 barrier 和 copy 合并到一次同步提交。保留宿主等待上一帧的责任，未引入 staging ring 或跨帧复用上传内存。 |
| Q32 | 字体信息改为私有强类型 Pimpl，消除 byte vector reinterpret_cast 对齐/对象寿命隐患。 |
| P19 | localtime 静态缓冲有线程风险，改为 localtime_r；没有增加可能错过时区变化的分钟缓存。 |
| Q24/Q29 | 删除相同分支的 topology 三元、忽略 style 的 sidebar 重载并迁移调用者。 |

额外修复：CPU 候选加载曾在工作线程调用 Controller::accept，可能修改共享错误状态，且失败后仍返回 ok。现在仅验证自己的候选，失败携带 session/geometry/node 来源，不发布失效会话。清理 previous 时先销毁借用实例再销毁程序。

确认无调用并删除：FontMetrics::height/initialized、FontAtlas::width/height、Context::mouse_down/mouse_pressed/consume_scroll、Controller::background_phase/set_current_character_index、PlaybackProgram::physics_mode（及多余 mode 状态）、AnimationProgram::timeline_label、未再使用的 Vulkan 上传/销毁 wrappers。同步切换接口仍被 player 的实际窗口诊断使用，保留；仅搜索常规播放路径会误判为死代码。

## 不成立或不能按原建议执行

| 原编号/建议 | 原因 |
| --- | --- |
| Q14/B18 | audio → application 是规定的允许方向；禁止的是 application 反向依赖设备实现。 |
| Q15/B2 | 配置和目录语义属于 application，私有 JSON 编解码不要求移动业务到 asset_io；公开头不得泄漏 JSON。 |
| Q20/Q23 | reaction_label 借用 program 字符串，没有临时 string UAF；icon 每次立即 flush，当前没有多纹理混画。移除多余 icon texture vector，保留即时顺序。 |
| Q11/S3-4 | UI 与 Controller 当前同一串行线程。为尚未使用的 UI 线程加命令队列没有当前收益。 |
| Q21/Q22 | const 缓存和借用字符串不是独立的线程许可；应按 ABI 的串行及借用寿命契约审查，不能单凭 const/mutable 宣布竞态。 |
| P23/S1-8 | 全面关闭 Release GL 检查同时隐藏资源/绘制失败，GLM 改动撤销。glGetError 不等于 glFinish；没有实测不能断言每次强制同步。 |
| P24/S1-12 | 导入器不能校验未来动画结果或外部 ABI 帧，保留有限值/索引/范围检查。 |
| P25/S1-13 | RenderFrame 是借用 span，move 不转移所有权；prepare 后所需命令需自持有快照。 |
| G13 | 字体 padding 必须清零，nullptr 上传会使线性过滤读取未定义内容。 |
| P1/S1-3 | 固定八级父链人为限制受支持层级，不可直接采用。 |
| P27/P28/P35/P36/G12 | glBufferData 可用于 orphan；直接 SubData 可能等待 GPU。批量 UI 必须保序/保存颜色和裁剪。render pass 依赖属于 Vulkan 宿主兼容契约，不能单边收窄或盲目合并遮罩。 |
| S2-2/S2-3/S2-5 | alpha 包围盒不保证保留 icon 透明范围及采样边距；BC7 有损且需能力检查。现代 NPOT 支持 mipmap，不要求 POT。 |
| S2-7/S2-9 | 线程安全、内存峰值、异步上传同步均需验证，不能承诺加载时间除以核心数或 PBO 保证不阻塞。 |
| S4-5/S4-8/B16 | 无显示构建仍使用 asset_io/UI 等依赖；开发期只支持当前 ABI。跨构建目录共享 GPU 锁用于避免并行采样，是有意设计。 |

## 呈现采样边界

优化前后 player FIFO 实时采样都超时，诊断拦截显示等待点是 vkQueuePresentKHR，不是 CPU 求值或 map/unmap。测量入口显式选择设备支持的 IMMEDIATE 并验证实际窗口，普通播放仍为 FIFO；不能用不同呈现策略的采样宣布后端加速。Khronos 说明 [present 可以阻塞](https://docs.vulkan.org/refpages/latest/refpages/source/vkQueuePresentKHR.html)，[IMMEDIATE 不等待垂直消隐且可能撕裂](https://docs.vulkan.org/refpages/latest/refpages/source/VkPresentModeKHR.html)。本轮未认证本机普通 FIFO 未聚焦行为，保留为宿主/驱动独立问题。

## 尚需测量或独立任务

P46 重复几何编译确实存在；资源缓存、纹理压缩/重打包、SIMD、排序/变形链缓存、布局缓存和更细 GPU 状态缓存需要代表性测量、内存边界及图像证据。配置保存异常事务、音频设备连接阻塞是真实风险，须分别定义失败行为和服务生命周期。ABI/Controller 按文件行数拆分不自动改善内聚，禁止只拆文件或增建空接口来宣布优化。

构建工具缓存、PCH、mold/PGO、RelWithDebInfo 可按实际环境选用；不将缺少这些工具本身列为缺陷，不强制新 CPU 指令集，不引入历史 ABI 兼容目标。

验收、性能边界与下一步见 [实施建议](optimization-suggestions-2026-10-12.md)。
