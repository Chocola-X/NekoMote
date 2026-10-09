# G25/V17复核与正式继承整合

2026-10-09，基线 `b7fd79a`，核心来自Codex研究候选 `d297827`。用户授权复核后合入；本轮补齐放行所需代表及型号证据，将单边界0x7fc语义整合到正式runtime。嵌套排除仍明确拒绝，资产schema和镜像约定不变。公式/缓存契约见[语义](inheritance-semantics.md)。

## 他人工作复核

G25源码/binary/资产/参照/五子manifest及结果hash一致，27静态几何、22新增GPU、195非空初态证据有效。45组合仅本地输出；195/45缺逐argv/stdout/stderr，命令/遮罩/标量“全匹配”及“全部完成”说法过宽。浏览器存在，没有失败日志证明不可用。G22/G24未推进。

V17的27原图/裁图及静态观察可保留。三条带实际顺序default,-30,-15,-5,+5,+15,+30，且不是动态序列；撤回动态连续性认证，不改冻结raw。纠正已写回各报告。

## Codex补验及正式回归

| 范围 | 结果 |
| --- | --- |
| 其余12旧FAIL，default/body_LR30 | 24几何及GPU/WebGL通过；max几何0.000130372px，阈值0.001，顶点/三角形完整，图像容差2零超差 |
| 三代表idle120/body_LR30 | clock1000/seed1、作者步1、物理全1；三组最终帧/图及每组22物理通道×120步通过，最大标量5.96e-8 |
| 合成组合契约 | 新增rotation90、sync1/8、自身combine三个section，解析曲面/缓存/零分配/嵌套拒绝回归通过 |
| 正式Release/WERROR/LTO | 全构建通过；141 unit cases / 37706 assertions（真实导入已提供素材执行）；GPU renderer及3 ABI入口通过 |
| 正式195初态 | 195/195非空可求值，原15个加载缺口解除；独立冻结逐argv/raw/hash |
| 正式player | coconut-hawaii-a加载并120步截图成功，无旧6/3；正式preview同条件再对WebGL通过 |

raw：`build/evidence/codex-g25-integration/`，`integration-manifest.json`、`new-models/manifest.json`、`dynamic/manifest.json`、`release-initial/manifest.json`及各日志；源码/binary/asset/tool/参照/结果/机器信息冻结。源参考仓库只读，未修改素材或桌面配置。实际查看新增Hawaii及Coconut idle120对照，手臂/衣袖匹配。几何和最终截图PASS与完整动态认证分开；SDK原始GetVariable不含差分，标量只比较显式物理绑定，不混用差分后的body通道。

节点挂接位移按evaluate缓存，顶点复用，合成稳态零分配；正式Release Coconut/body_LR30 CPU evaluate+build_frame，预热120/4000样本，P50/P95/P99=0.379661/0.404664/0.422823ms；记在release-benchmark.stdout，未声称应用性能改善。45组合参照、其他动作/过渡、暂停恢复、locator/触摸、实际应用分配和长期运行仍待验。以上为该阶段历史范围；G22/G24/G26后续执行与独立复核见[当前复核](review-g22-g26.md)，最新分工以[交接](agent-next-handoff.md)为准。
