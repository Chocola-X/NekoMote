# 优化实施与后续建议（2026-10-12）

以 [复核结论](optimization-audit-2026-10-12.md) 为准，不继续执行原报告的四阶段全量改造。每项性能收益须测量；正确性检查、所有权和宿主同步不作为可删除的开销。

## 本轮实现

- 修正无副作用配置路径探测、HOME 判空及 Qt 线程 CPU 诊断。
- CPU 候选加载不再写 Controller 状态，初始化失败不返回成功。
- 保留经回归的零角度/UV 倒数/整数幂；缓存 UI uniforms 和字体 ascent，文本容量复用。
- Vulkan 显式持久映射和单次提交纹理上传；普通 buffer 仍仅分配，截图按自身生命周期映射。
- 删除已确认无调用的函数、重载和辅助 wrappers；保留真实窗口诊断使用的同步切换接口。
- 修复 Vulkan 测量入口的呈现策略：FIFO 在本机 NVIDIA/X11 未聚焦窗口停于 vkQueuePresentKHR；测量显式选择设备支持的 IMMEDIATE，与 GL swap interval 0 对齐。不支持时明确拒绝该测量，普通播放仍使用 FIFO。Release player 通过 NEKOMOTE_VK_VALIDATION=1 要求验证层，缺失则失败。
- 同步此前拖动变更的测试与契约；更新 Plasma 的小目录 UI 测试，不再使用旧八组菜单坐标，明确建立可滚动内容。

原 GLM 的 Release GL 错误检查关闭已撤销。没有新增压缩格式、全局缓存、异步服务框架或后端万能工厂。

## 验证

证据根目录：`build/evidence/optimization-20261012/`。基线来自 `git archive 3a56ce0` 独立构建，优化版 Release/LTO/WERROR。

- 当前 CPU、GPU、Plasma 隔离测试及性能汇总见 [机器记录](validation/optimization-results.json)。失败尝试单独保留，不覆盖为通过。
- 固定 seed/步数/尺寸比较优化前后 GL/Vulkan 的模型、背景和完整 UI；原 WebGL 同初态截图独立对照。
- CPU 几何测量覆盖 Chocola date-a、Fraise maid-b，预热 120、每轮 10000 样本、基线/优化版交替三轮。仅覆盖 evaluate+build_frame，不能推定整个壁纸功耗/内存收益。
- player 的实时性能只按成功且同条件的采样报告。窗口呈现/宿主暂停导致的失败或超时不计入收益；Vulkan 同步验证和图像通过不替代 FPS 测量。
- 稳态零分配沿用真实的 runtime/render/UI 分层计数检查，驱动 malloc 和加载边界不在该计数内。

## 下一批（GLM 可独立执行）

1. **G34：消除 Session 的重复几何编译。** compile_session 当前先编译几何发现 gaze/lip 绑定，再 compile_playback 重编译。先冻结单模型和代表模型集的加载耗时/峰值；明确编译场景的唯一所有权，供动画/物理复用。不从字段名另猜变量来源，不加隐式空场景 fallback。保留现有来源发现、编译错误、物理借用销毁顺序。模型/会话测试、真实加载以及同输入帧/截图必须一致。改动限 application/session 与 runtime 编译契约及相关测试，独立提交由 Codex 复核。
2. **配置保存事务**另行冻结契约后实现：保存失败要明确报告，不能让 CPU 已切换而 GPU/渐变仍停留旧状态。不要将 catch 全吞异常当修复。
3. 加载峰值/字形内容变化/帧热点先记录 profile，再选择有界缓存或数据布局；不给图片压缩、SIMD、父链缓存提前承诺收益。

Gemini 可做固定条件的文字、透明边缘、菜单展开及模型遮罩截图复核；不修改 GPU 同步。G33 基线分发与真实 miku Vulkan、跨驱动/Wayland、多屏/长期仍保持各自验收状态，不由本轮优化代替。
