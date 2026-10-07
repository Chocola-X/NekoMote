# 当前缺口

更新：2026-10-07。已经闭合的修复不再列为待办；成果与验证范围见 [整合摘要](review-agents-current.md)，任务所有权见 [协作清单](agent-workboard.md)。Fraise代表add_mask、ABI主边界、背景共享时间预算及C++候选事务已通过对应验证。

| 范围 | 当前缺口 | 所有者 |
| --- | --- | --- |
| 非标准混合/属性 | bm19/attr2仍拒绝。milk-summer默认保存角色因此加载失败；源GPU零步证据已交付，原生映射及更多姿态未验收 | Codex C5，Gemini V6 |
| 局部继承/遮罩 | 0x7fc近似已撤下；Coconut候选8.876191px、Azuki候选.0064996px均未过.001px门槛。Fraise-b嵌套/缺失遮罩范围未闭合 | Codex C5 |
| 默认启动/恢复 | 首帧诊断已补；不支持角色仍退出，缺恢复界面/目录能力标记；未知角色名的选择行为也需明确 | Codex C6 |
| UI | 状态8项通过不认证实际标题0px同步或淡出画面；动态参照/输入隔离未闭合，注视会穿透；字体及图元稳态分配未测 | Codex C6，Gemini V6 |
| 会话与资源 | 角色过渡墙钟/两遍over、背景reset/重入、声音候选失败和连续切换回收尚未验收 | Codex C6 |
| 目录/导入 | 覆盖/删除旧输出及向源写CPU报告已修复；已有输出保持EXISTS_UNCHECKED。完整195账本、逐项异常/检查点、输出来源和公共选择逻辑未验收 | GLM G7 |
| 诊断/构建 | C宿主墙钟推进、raw截图及参数/失败边界待修复；UI未登记CTest；无ABI构建问题未闭合 | GLM G8/G9 |
| 性能/稳定性 | 核心零分配不覆盖UI/整机；网页收益方法、同条件资源/功耗及8–24小时未认证 | 后续profile/长期阶段 |
| 桌面/发布 | Qt Quick实际图形宿主、Plasma、多屏卸载/重建、195视觉/交互及安装包未验收 | Codex C7及P9/P10 |

正式拒绝是开发期保护，不是最终功能完成。不得通过放宽阈值、型号特判或近似开关消除上述缺口；源码及当前资产需要真实WebGL证据。

## 素材来源限制

coconut-dress已有外部纹理修复历史，但解包工具/版本未知，全姿态未认证。G7账本需保留来源，不能把本地存在等同于受控输入验收：

| 来源 | SHA256 |
| --- | --- |
| data/coconut-dress.pure.psb.zip | 6dfc2bb95c098267081f6cd3d58d612fecb199ffc4331637d3a38a03ab257d9d |
| model_conversion/coconut/coconut-dress/coconut-dress.json | 63b02025535aa6d6f301e6e6cba9f9b73f5e2d40ddb1f7007c0f8d518272e19e |
| coconut-dress-a_tex000.png | f6d32dafa122de947640730b40eefa29e20dd2292648f96a9f8a3999c2821e1c |

历史Coconut“93%差异”曾混用背景/透明参照，不能作为当前误差；真实手部失败以冻结同条件几何/图片为准。缺素材型号仍占195账本一行，不从分母删除。
