# G20：被否决继承候选的证据记录

执行CodeArts / GLM-5.2，base`c241af8`；Codex复核基线`82ab3b6`。本页保留失败试验，不是当前实现或待办。单边界0x7fc后来已[正式整合](review-g25-integration.md)，旧15型号加载拒绝已解除。

旧候选v3阻断curve_parent，先deform父世界原点再加子相对偏移。三default误差<.001px，但body_LR30误差8.945889/1.143875/.101425px均失败；Codex独立重算Coconut为8.945888519px，37 primitives、missing0。旧“纯X残差”解释撤回：Coconut/Azuki的Y残差约1.506/.420px。

`native_trace.trace.json`与WebGL trace相同，不能作独立native证据；实际比较来自nekomote_pose原生求值。158.91/body5/body15等不完整原始值不作验收。raw在`build/evidence/g20-inheritance/`及当期review目录，失败候选仅用于溯源，不按标签或常数补偿恢复近似放行。正式公式见[继承语义](inheritance-semantics.md)，现阶段不再分派G23或执行旧候选工作树。
