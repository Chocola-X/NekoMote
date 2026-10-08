# G13 全目录触摸与截图覆盖运行

更新：2026-10-08。执行 CodeArts / GLM-5.2。基线提交 `357289d`。
使用 Codex 复核后的冻结工具（`7c055dd` 后），不改工具、核心、区域数据或当前摘要。
执行状态见 [当前摘要](review-agents-current.md)，工具契约见 [G12交接](g12-touch-handoff.md)。

## 运行命令

```sh
python3 tools/touch_audit/run_touch_audit.py --reference-root ../NekoWebShow \
  --assets assets --probe build/release/bin/nekomote_session_probe \
  --player build/release/bin/nekomote_player --capture \
  --output build/evidence/g13-touch-audit-2026-10-08/full-catalog
```

Fraise B 先行小包在 `build/evidence/g13-touch-audit-2026-10-08/fraise-preflight/`。
全目录长跑约 27.5 分钟（15:27–15:54 CST），退出码 0。

## 冻结标识

| 项 | 值 |
| --- | --- |
| head_commit | `357289dec3d73337c6fd8c17705599e864df050b` |
| tool_sha256 | `b2d6fc5335030154e9acd939a49ae6867b1422ef67bd5dd7fc1936ce21d6f173` |
| probe_sha256 | `8b2e877a1ff075ea7a51a7b1203bfcc2a9a1700ffac37ff81549a091e32f0326` |
| player_sha256 | `04c0477efb0664d7ba4c7ce685ed02c92a7c992a03b7b49ec3863f18c0e22245` |
| catalog_sha256 | `72bd4ad864c83d25...`（195 项） |
| catalog_total | 195 |
| scope | full_catalog |
| complete | True |
| all_requested_checks_passed | False（15 项 CPU 求值失败） |
| freeze_errors | [] |

## 结果

| 状态 | 数量 | 说明 |
| --- | --- | --- |
| OK | 180 | 四条件全通过，zone_tests + outside_test + capture 齐全 |
| FAIL | 15 | CPU 求值失败（region/initial state probe failed），四条件全失败 |

15 项 FAIL 与 G12 冻结账本一致，均为已知 CPU 求值失败模型：
azuki-maid-b, azuki-maid-b-custard, azuki-santa-b, azuki-wintermaid-b,
cinnamon-maid, coconut-dress, coconut-dress-a, coconut-hawaii-a,
coconut-maid, coconut-maid-a, coconut-maid-a-4yo, coconut-santa,
coconut-swimsuit-a, coconut-wintermaid, coconut-yukata。

## 证据完整性核对

逐行确认请求/响应/返回码/SHA 可读取并匹配，结果：

| 检查项 | 数量 | 缺失 |
| --- | --- | --- |
| 条件 | 720（180×4） | 0 |
| zone_tests（center+inside） | 7200（720×5×2） | 0 |
| outside_test | 720 | 0 |
| 截图 plain+overlay | 1440（720×2） | 0 |
| result.json（含 returncode） | 8700 | 0 |
| 资产 SHA（model/session/texture） | 可读且文件存在 | 0 |
| 来源 SHA（source_resource） | 可读且文件存在 | 0 |
| 工具/二进制 SHA | 可读且文件存在 | 0 |

证据包 7.5 GB，59341 文件，1440 PNG。所有 result.json 包含 returncode 字段。
FAIL 模型 60 条件（15×4）无 capture，错误明确记录，不作为 MISS。

## pant 区域可见性

原报告把中心输出坐标当成PNG坐标，撤回“720个pant全在画幅内”的结论。
中心输出点转换PNG需加半画幅（800×1080时加400/540）；独立重算635/720个pant椭圆完全在画幅下方，其余相交也不证明角色部位可见。
CPU几何命中与画幅可见性分别记录，Gemini V12按实际图观察。

## 四条件

default_idle（默认姿态）、idle_120（固定待机120步后触摸，事件121）、
moved（水平移动）、scaled_half（半缩放）。固定 seed=1。
idle_120 先推进120次更新再触摸，不把事件120之前的119次更新称为120步。

## 限制与交接

- 本任务只运行冻结工具产证据，不改工具/核心/区域数据/当前摘要
- 180 OK 仅证明 CPU 几何命中与反应优先级正确，不认证视觉画面
- 15 FAIL 是 CPU 求值失败，交 Codex C5 处理
- 逐型号视觉观察交 Gemini V10，使用本冻结包的 plain/overlay 截图
- pant 及其他区域的实际可见性、动作变形、优先级重叠交 V10
- 两旧候选（Coconut 48 作者单位 Y 变化）没有验收，不应用

后续按[当前协作](agent-workboard.md)执行；G13本包已独立复核，不重复长跑。
