# C6 输入与 G11/V8 复核交接

更新：2026-10-08，基线 `7a9b39a`。Codex / GPT-6（精确变体未暴露）。
任务：复核 G11/V8、用户授权 GLM 注视修复，处理 Fraise 法国女仆 B 的触摸反馈，
交付实际区域调试显示。分工见 [协作清单](agent-workboard.md)，机器摘要见
[验证结果](validation/c6-input-review-results.json)。完整引擎/Plasma 尚未最终验收。

## 核查结论

- GLM `1343172` 的 gaze 修复正确：上轮 C5 已在几何入口反向镜像参数，宿主不能再次反向。
  本轮补 enter/move 方向测试；正式呈现镜像保留。
- G11 27 项 NO_PNG 推断错误，实际有 30 张外置 PNG；源映射、资源错误假 MATCH、
  尺寸/字节关联、CLI 分支及 provenance 已修。旧账本/证据保留，新冻结工具重新采样
  全目录为 194 MATCH / 1 已知 Coconut dress MISMATCH，退出非零正确。
- V8 图像/几何证据保留；修复标量 zip 截断、缺值/空样本假通过、Chocola 漏测、
  输出覆盖和反例假 PASS。重新采样 22 项：20 标准通过，2 标量 FAIL，全部几何/图像通过。
  静态 11 项标量为 NOT_APPLICABLE；11 动态项实际 9 PASS / 2 FAIL。
  Chocola 两项各比较 1416 个标量，max=0。SDK step0 公共缓存与原生已配置状态不同，
  显式记初始边界；双方仍检查完整步号/数量，严格比较 step1..N。

## 输入契约与修复

`SessionInstance` 的 gaze/touch 均接收**呈现方向的中心输出像素，Y 向下**。
宿主仅将逻辑窗口坐标转 framebuffer 像素并减去半个画幅，不能额外镜像。

触摸旧路径对整个点击 X 翻转，再由 session 减 origin，导致镜像模型水平移动后
出现两倍 origin 的偏移。现在 session 先减位置、除 session_scale×author_scale，
再对作者区域逆向镜像。非镜像、负/正位置、缩放共用同一契约；C ABI 调用者同步文档。
未更改模型、触摸区域配置或外部素材。

这闭合坐标错误，但不证明静态触摸椭圆已贴合每个姿态。原导入器与 WebGL 公共
getTouchRegions 同样使用作者标记或 eye/mouth 估算；它们不是变形网格。
数据细小偏差可现在核查，数据调优应在同模型坐标/姿态验证稳定之后应用。
不要用单帧数据偏移修补全局映射，也不要宣称所有触摸问题已解决。

## 调试显示

```sh
./build/release/bin/nekomote_player --assets assets \
  --character fraise-france-maid-b --show-touch-regions --no-save
```

胸红、眼黄、脸绿、头青、下身紫；半透明填充加边界。只显示有配置反应的 zone，
顺序为胸/眼/脸/头/下身，与实际触摸选择优先级一致。Busy/暂停、UI 拦截或输入不可用
不代表这些静态椭圆会消失，也不保证当时能立即触发。

`--show-touch-regions` 只在诊断入口创建独立 GPU renderer，预分配顶点/索引，
未开启时不创建或绘制。诊断与性能采样互斥，没有常态读回。
`SessionInstance::touch_regions()` 为同映射投影的固定大小值，probe 输出同名字段；
图上圈线是 128 段可视化，不改变实际椭圆公式。

固定截图示例（显式 model/session 模式避免用户保存布局影响证据）：

```sh
./build/release/bin/nekomote_player \
  --model assets/characters/fraise-france-maid-b/model.nkm.json \
  --session assets/characters/fraise-france-maid-b/session.nks.json \
  --width 800 --height 1080 --origin-x 80 --origin-y 350 --scale .8 \
  --show-touch-regions --transparent --mute --no-save --steps 0 --capture <新PNG路径>
```

`nekomote_session_probe MODEL SESSION INPUT_JSON OUTPUT_JSON` 输入同一 origin/scale。
PNG 点转 probe 输入需减 `[width/2,height/2]`，不做镜像。只用 session 事件/应用回调，
不向系统注入输入。真实反应各从独立初态检查，避免 Busy 被误认为 miss。

## 验证与范围

证据：`build/evidence/c6-input-review-2026-10-08/`，素材及 PNG 不入 Git。

| 范围 | 结果 |
| --- | --- |
| 构建 | dev/release/无窗口 ASan，WERROR 通过 |
| 测试 | Debug 145/145（GPU 串行）；无显示 ASan/UBSan 143/143；Python 96/96 |
| 新应用契约 | 镜像/非镜像、非零位置、作者 scale 2×实例 .75、椭圆边界内外、gaze enter/move，通过 |
| 图集重审 | 195 行 / 202 张 PNG，194 MATCH / 1 MISMATCH；原漏审 27 项补齐 |
| V8 重审 | 20 标准 PASS / 2 标量 FAIL；整套验收 false，工具退出非零 |
| 调试截图 | Fraise B 默认/移动缩放、Chocola date-a、Milk summer：原图/覆盖图及实际区域 JSON |
| 真实触摸 | Fraise B 移动缩放后胸部中心、左右内边缘触发，旧镜像偏移位置不触发 |

已读取 Fraise 两张覆盖图核对颜色、边界及位置；同份参数由 CPU 投影和实际命中检查复核。
独立执行者只读检查输入逆变换、renderer 容量、所有权和 GL 生命周期，未发现新增阻塞。
全目录触摸画面、UI 隔离、功耗/长期资源未认证，本轮不宣称性能改善。

工具首个 V8 调用缺必需 browser，在采样前明确退出；失败目录保留，不覆盖。
最终 V8 使用新目录 v8-accepted。G11/V8 非零退出分别对应已知来源差异、标量反例，
不是工具执行崩溃。后续 GLM G12 先准备覆盖账本，Gemini V9 独立逐模型核对并交候选数据。
