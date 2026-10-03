# P5 时间线调查与缓动内核（进行中）

2026-10-04。P3/P4 尚有明确回归，P5 只推进不依赖它们的语义实验，本记录不认证动画播放器。

已实现 scalar `ease_progress`，正 easing=e 为 t^(1+e)，负值为 t^(1/(1-e))，端点保持精确。六组实际 SDK 变量轨迹（0、±1、±2.5、−10）冻结在 tests/fixtures/animation_curve_reference.json；归一化容差 5e−7，包含源/请求 hash，不用生成公式替代参考值。Debug/Release 43 项、ASan/UBSan 42 核心项通过。

## 已观察到的时间线规则

- type=2 的变量关键帧启动朝该 value 的过渡，而非传统相邻值插值。sample_05 的 body_UD 第一个目标 30 在第 13 帧达到；第 14 帧已开始朝 −20 的第一步；第 27 帧达到，第 28 帧保持。第 29 帧开始朝 −10，第 51 帧达到，第 53 帧停止。候选 duration=next_time−key_time−1，需要更多边界/空帧/循环实验。
- Play 后、第一次 Update 前的 raw 变量仍为初态；停止后主动作的数值保留。
- 120×0.5 与 60×1 在同一整帧边界仍不同（最大 raw 参数差 2.500000238），与事件触发后过渡推进顺序有关。原生应独立冻结作者时钟，不能让显示刷新率决定分区。
- SDK IsLoopTimeline/GetTimelineTotalFrameCount 对该模型所有 115 条时间线返回 1/0，实际 sample_05 在 53 帧停止。采样工具字段明确为 reportedLoop/reportedFrames，不用作真实循环/时长。

## 控制器与隔离实验

SDK hair/parts/bust scale=0 **不停止控制器积分**。初态零步的 P2/P3/P4 基线有效；带步进不能据此声称禁用物理。新增 --disable-physics/--disable-auto-blink 内存 PSB fixture 仅修改对应 enabled/blinkEnabled 字节并记录 hash，不改外部文件。

原 PSB sample_05 在 60 帧时 bust_UD=−18.189874649（CountVariables 没列出此绑定，需要 --observe-variable）。相同普通变量、零步静态参考与原生几何最大差 0.000371263px；关闭物理/眨眼的九字节 fixture 在 60 帧时也为此误差；原始动态参考则胸部两资源达到 7.78026px。该差异定位为控制器，不能改变形公式掩盖。type=1 bust clamp 非零仍明确拒绝，尚未验证。

关闭控制器的 idle 120 帧 PNG 与零步默认图逐字节相同。新增采样选项后的未修改 PSB 零步图也与既有 default.png 逐字节相同（SHA256=e23aa19c310b9f0f069d72ed00324eca8d2e2f5cca5427f936fa851f4585a793）。

眉毛的 edge/node 是实际过渡路径规则：0→40 会经过低值段后跳到 31 段，而非经过所有表情；40→0 反向请求会先向 49 走。mouth 目标在第一次更新即切换，immediateControl 在请求时立刻切换，face_talk 仍随 duration 插值。instantVariableList 不是统一瞬时切换策略。规范化控制元数据与验证路径图后再实现完整播放器。

复现入口见 tools/reference_capture/README.md；采样来源、输入与图片 hash 见 docs/validation/p5-animation-observations.json。完整源模型动态验收必须保留物理/眨眼；隔离 fixture 只认证指定模块。
