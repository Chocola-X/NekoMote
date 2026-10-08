# G11 图集来源审计与复核

更新：2026-10-08。原执行者 CodeArts / GLM-5.2，提交 483a3ff；
用户授权注视修复 1343172。Codex 复核与修补见 [C6 交接](c6-input-review-handoff.md)。

原目录工具、195 行历史账本及合成测试保留。历史账本为
`docs/validation/g11-atlas-source-ledger.json`；其 167 MATCH、27 NO_PNG、1 MISMATCH
是交付时记录，不是当前完整验收状态。原 /tmp/g11-atlas-output 不可读，不能宣称重新读取原捕获。

## 已修问题

- 27 个 NO_PNG 目录实际有 30 张 PNG，源 JSON 的 texture.pixel 为 null。
  resx Resources 为空不证明内嵌纹理。工具改用已存在的精确外置纹理映射，无法解析时明确未认证。
- 缺失/损坏资源不再被过滤后以剩余匹配覆盖为 MATCH；关联同时核对尺寸、RGBA8 字节数与 SHA。
- 两个旧 CLI 覆盖负例以前因缺 catalog 提前拒绝；新测试达到对应覆盖分支。
- 记录 PSB zip/payload、JSON/resx/PNG、driver、浏览器和完整工具依赖，核对捕获的实际模型/PSB/driver。
  捕获 JSON/PNG 留在项目证据目录，命令、hash 和逐项错误可复核。

局部测试 37/37（原 23 + 14 新负例/契约）。新工具冻结后重新采样全部 195 项：
**194 MATCH、1 MISMATCH**，均保留在分母；原 27 项已完成实际上传核对。
证据：build/evidence/c6-input-review-2026-10-08/atlas-ledger.json 及 atlas-captures/。
因已知 MISMATCH，审计命令正确退出非零，不描述为整个来源全部通过。

Coconut dress 唯一差异仍为外部 PNG 与 PSB 实际上传不符；项目内已有核实的正确图集。
本轮未再次修改外部素材。图集一致不认证模型画面、交互或长期运行。

## 用户授权的注视修复

1343172 正确移除 player 对 gaze 的额外 X 翻转：上轮 C5 参数镜像已在几何入口执行，
宿主再反向会造成鼠标与注视相反。上轮迁移遗漏由本轮补齐测试。

GLM 当时保留 touch 翻转属于该提交的范围；不能据此认定触摸坐标链全部正确。
本轮发现其在减去非零 origin 前翻转导致区域位移，现统一由 session 做逆变换。
**gaze 与 touch 的宿主输入均为呈现方向中心输出像素，不额外镜像。**
静态触摸数据与轮廓的细微偏差由 Gemini V9 独立核查，不用数据修改掩盖坐标错误。
