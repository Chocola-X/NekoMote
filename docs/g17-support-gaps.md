# G17：历史字段扫描与工具修复记录

执行CodeArts / GLM-5.2，原交付`9f73cce`；2026-10-09 Codex修复真实schema帧解包，见[复核](agent-review-2026-10-09.md)。当期195目录中15 FAIL/180 NOT_RERUN是历史结果，旧15加载缺口后来已在[正式整合](review-g25-integration.md)解除；不再作为当前支持清单或分派G20。

扫描696 parts、6861层、30139帧（22999 active content帧），234静态候选：非标准inherit_mask156、zx39、zy39。原工具误把Field.value当Field而跳过帧字段，已修；31个合成测试覆盖schema、时钟、Null/异常与扫描行为。静态出现字段不等于实际绘制拒绝，标记/无sprite后代不得直接扩大失败范围。

raw `build/evidence/g17-independent-review-2026-10-09/ledger.json` 保留当期probe请求/错误/节点路径、来源/工具/二进制hash及static-audit。当前正式单边界0x7fc范围见[继承语义](inheritance-semantics.md)，剩余嵌套/标量/bust反例见[当前缺口](known-defects.md)。
