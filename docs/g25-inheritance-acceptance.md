# G25：扩大验收（静态部分完成，已复核）

CodeArts / GLM-5.2，候选base `d297827`，提交 `f054c8a`；Codex于2026-10-09复核。后续正式整合及补验见[整合记录](review-g25-integration.md)。

| 范围 | 实际证据 |
| --- | --- |
| 27静态几何 | 点/顶点/三角形及完整帧坐标通过，max 0.000136479px；没有单独证明命令/遮罩/标量全匹配 |
| 22新增GPU（加Codex5，共27） | alpha差0，黑白合成RGB≤1，零像素超容差2；图/hash与compare结果一致 |
| 195初态 | 唯一195型号，非空帧、frame_error=0，15旧FAIL可求值；这是CPU结果 |
| 45扩组合 | 本地非空输出，没有参考trace/图比较，不能计正确性PASS；bbox响应也不证明局部眼/口/胸正确 |
| 三代表CPU基准 | 4000样本、预热120，evaluate+build_frame P50约0.29–0.38ms；无真实模型分配计数，非应用性能结论 |

源码/二进制/资产/27参照及现存结果hash已复核一致。raw在 `build/evidence/g25-acceptance/`；`frozen-manifest.json` 与五子manifest真实。初态195、扩组合45缺逐argv/stdout/stderr，旧“逐条全部保留”撤回；旧machine.cpu是运行模式文本，不能当硬件型号。同源码不保证二进制hash相同。

G25当时未补齐的12型号静态与三代表idle120后续已由Codex补验并正式整合，见[整合记录](review-g25-integration.md)。G26有42静态PASS/3 FAIL及9动态终态证据；G27/G28/G29/V19也已提交。各后续报告独立验收范围见[摘要](review-agents-current.md)，不再分派旧任务或声称G22/G24尚未开发。
