# G20：部分继承候选与证据

执行CodeArts / GLM-5.2，base1b02ce8；Codex复核基线82ab3b6。正式0x7fc拒绝保持，15型号仍不支持。

已有三代表（coconut-santa、azuki-santa-b、cinnamon-maid）6腕链、8组原PSB/WebGL PNG/JSON/trace和6候选CPU结果。隔离worktree `../NekoMote-g20-candidate` 的geometry_scene.cpp仍未提交；保留该工作，不作为正式实现。

实际当前候选为v3：阻断curve_parent，先deform父世界原点，再加子相对偏移；原报告主体v1直接deform子世界原点与当前源码不一致。三default几何误差<.001px；body_LR=30分别8.945889/1.143875/.101425px，均不通过。Codex独立CPU重算Coconut得8.945888519、37 primitives、missing0，命令/二进制/源码/trace hash已保存于本轮review目录。

撤回“纯X/Y完全匹配”：Coconut tex0078有约1.506px Y残差，Azuki约.420px。不能据纯X假设推导公式。native_trace.trace.json与WebGL trace相同，不是独立原生证据；现有正确数值比较由nekomote_pose实际原生求值对WebGL trace完成，并不使用该文件。158.91、body5/15候选数值及其他变体没有完整原始结果，暂不采信。

G20是部分研究交付，未完成195静态来源、多变量/固定步矩阵、完整根链/资源域/control-point原值、候选截图差分及全部冻结。当前证据在 `build/evidence/g20-inheritance/`；准确公式和支持恢复仍未完成。

下一步GLM G23优先补可重复CPU链/XY残差与独立参照、冻结候选；Codex审公式和代表抽检，过几何/拓扑/图像门槛后才进入正式路径。不能按型号、标签或近似fallback放行。
