# G23：旧继承候选记录（已复核）

原执行：CodeArts / GLM-5.2，base `c241af8`，报告提交 `b11e175`。Codex于2026-10-09复核并纠正；当前公式/进展以[接管记录](codex-inheritance-review.md)为准。本页保留失败证据入口，不作为下一轮实现依据。

| 候选 | 行为 | 实际结果数 |
| --- | --- | --- |
| v1 | 分组world原点随父曲面变形，阻断父顶点曲面 | 27 |
| v3 | 父原点变形后加子局部偏移，阻断父顶点曲面 | 27 |
| v4 | 阻断父曲面，不计算挂接位移 | 27 |
| v5 | v1另含直接父warp | 21（缺三代表UD/SL） |
| v6 | 保留父曲面逐顶点变形 | 27 |

总计129份，不是完整135。代表Coconut body_LR30：v1/v3/v5约8.946px，v4约16.106px，v6约160.190px；都未达0.001px门槛。v1/v3局部translation为0时相同；v5在已运行条件下直接父warp恒等，不能宣布通用等价。

参照SL条件实际设置`body_slant=30`并改变画面，候选低误差不证明“不触发”。旧12连续warp链、全部纯X残差及44%幅度不足推断撤回：combine已经聚合helpers，挂接点需由子节点自身曲面和局部偏移解析，不应倍增位移。

raw：`build/evidence/g23-inheritance/manifest.json`、`matrix_results/`、`residual_analysis/`；旧隔离候选在 `experiments/g20-*`。原报告完整历史保存在Git。新Codex候选 `d297827` 已通过27静态几何/5GPU条件，但仍未正式放行；扩大验收交G25。
