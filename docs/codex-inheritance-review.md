# G23复核与核心求值接管（历史候选阶段）

当前已正式整合，最新状态见[整合记录](review-g25-integration.md)。本页记录候选阶段，不能据旧15FAIL判断当前运行时。

2026-10-09，审查基线 `b11e175`。Codex接管公式推导与候选实现；GLM继续批量采样、工具和验收，Gemini核查视觉。旧候选已有数值误差，尚未到依靠目视决定是否通过的阶段；不能将失败归因为缺少多模态能力。

## 复核结论

- G23实际129份结果（v1/v3/v4/v6各27，v5只有21），不是135；缺v5的三代表UD/SL六条件。源码/二进制/三资产/27参照hash已复核，完整运行命令和输出冻结不足。
- v1/v3相等依赖该组节点局部translation为0，v5还依赖直接父warp恒等，不是通用等价公式。SL标签实际变量是`body_slant`，参照确实改变，不能据候选误差小断言“不触发”。
- helpers经combine聚合，实际curve_parent链不是报告所称12个连续warp。旧“只修正44%”是错误挂接点推断，不应按幅度补偿。残差也并非所有手部都纯X。

## 候选结果

研究分支 `codex-inherit-analysis`，提交 **`d297827`**，工作树 `experiments/codex-inherit`。公式和测试见该提交的 `docs/inheritance-candidate.md`。子节点保留自身曲面，父曲面只提供子节点自身变形后原点的整体位移；每节点缓存，未经过排除的原路径保持原浮点运算。未验证嵌套明确拒绝。

| 验证 | 实际结果 |
| --- | --- |
| 三代表27静态几何条件 | 27通过；最大0.000136479px（门槛0.001），完整帧/顶点/三角形无缺失或多余 |
| 5实际GPU对WebGL图 | alpha差0，黑白合成RGB≤1，零像素超容差2；Codex实际查看三代表手部/衣袖对照 |
| Release/GCC15/WERROR | runtime、pose、preview、unit构建通过，格式检查通过 |
| 单元测试 | runner 141 cases / 37520 assertions；1真实导入用例因素材变量缺失未执行，不能计真实素材PASS；新增2合成回归，迁移1旧拒绝契约 |
| 候选CPU基准 | Coconut santa/body_LR30，预热120、4000样本；P50/P95/P99=0.374214/0.411858/0.453903ms，evaluate+build_frame范围，非应用性能改善结论 |

证据 `build/evidence/codex-inheritance-review-2026-10-09/`：`cached-matrix/manifest.json`、`gpu/manifest.json`、`unit-all.log`、`benchmark.json/stdout`；source/binary/asset/reference/result hash及argv已保存。参照沿用GLM冻结图/trace，未重采WebGL。工具`certified:false`保持。v7/v8失败假设与一次明确标记的旧二进制误跑保存，不计通过。

候选阶段已结束，`920245e`正式整合单边界0x7fc并解除旧15加载FAIL，见[正式整合](review-g25-integration.md)。本页不再分派G25或要求主分支保持旧拒绝；剩余语义及应用验收按[当前任务](agent-next-handoff.md)执行。
