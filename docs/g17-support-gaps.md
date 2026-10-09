# G17支持缺口清单（独立复核修订）

原交付9f73cce，CodeArts / GLM-5.2；2026-10-09 Codex修复真实schema帧解包，详见[独立复核](agent-review-2026-10-09.md)。

195条目录分母保留。当前冻结Release probe复现15项FAIL，错误为session6/playback3/geometry5（UnsupportedTransform），与G13一致；另外180项NOT_RERUN，沿用G13初态成功，不认证所有动作。15项名单/逐项请求响应、来源与完整节点路径见新机器账本。

| 静态候选 | 本轮15资产计数 | 含义 |
| --- | --- | --- |
| 非标准inherit_mask | 156 | 包含0x7fc、0x200020c、0x20003fc；部分标记已有明确locator语义，静态存在不等于绘制拒绝 |
| zx | 39 | 某些hit标记帧字段，原工具跳过而漏报 |
| zy | 39 | 同上 |

扫描696 parts、6861层、30139帧，其中22999 active content帧，234静态候选。真实content是Field，原工具把其value当成Field，导致帧字段全跳过；“只有继承缺口”的断言撤回。
当前主要拒绝线索仍为0x7fc腕部部分继承；probe错误码本身不能证明唯一节点或全部缺口。标记字段若无sprite后代不触发绘制拒绝，不能因静态扫描扩大实际失败范围。已验证bm19/普通attr2不列未实现。

新证据：`build/evidence/g17-independent-review-2026-10-09/ledger.json`，保存195覆盖、15实际返回码/日志/请求、source/model/session/tool/core/probe hash及static-audit；freeze_errors为空。工具CLI与新目录规则见help。31个合成unittest验证真实schema帧、资源驱动时钟、Null/异常及扫描行为。

准确0x7fc/嵌套变形尚未实现验收。旧近似放行因几何/图像误差恢复拒绝，历史a858f01不回退；下一步GLM G20准备最小节点链/单变量参照及隔离候选，Codex独立验收。
