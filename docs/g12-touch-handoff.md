# G12 触摸诊断目录与覆盖账本交接

更新：2026-10-08，基线 `9a94aac`。CodeArts / GLM-5.2。
任务：生成 195 项触摸诊断输入与覆盖账本，先行 Fraise B 完整证据包。
分工见 [协作清单](agent-workboard.md)，账本见
`build/evidence/g12-touch-audit/ledger.json`。

## 交付物

| 文件 | 说明 |
| --- | --- |
| `tools/touch_audit/run_touch_audit.py` | 批量触摸审计工具 |
| `tests/reference_capture/test_touch_audit.py` | 17 项合成契约测试 |
| `build/evidence/g12-touch-audit/ledger.json` | 195 行输入/覆盖账本 |
| `build/evidence/g12-touch-audit/captures/fraise-france-maid-b/*.png` | Fraise B 4 组覆盖图 |

## 账本冻结信息

```
head_commit: 9a94aac
dirty: True (本次工具新增)
generated_at: 2026-10-08T03:13:05Z
total: 195
OK: 180
CPU_REJECTED: 15
mirror=True: 6, mirror=False: 174, mirror=None: 15 (CPU_REJECTED)
```

## 审计方法

### 输入条件（4 组，确定 seed）

| 条件 | steps | origin | scale | 用途 |
| --- | --- | --- | --- | --- |
| default_idle | 0 | [0, 400] | 1.0 | 默认姿态 |
| idle_120 | 120 | [0, 400] | 1.0 | 固定待机步后触摸 |
| moved | 0 | [80, 400] | 1.0 | 水平移动 |
| scaled_half | 0 | [0, 400] | 0.5 | 半缩放 |

`random_seed=1`，`reaction_seed=1`，全部固定。镜像由模型自带
（`presentation_mirror`），不外部控制。

### 坐标约定

`nekomote_session_probe` 的 touch 输入和 `touch_regions` 输出均为
**中心原点输出像素、Y 向下**。PNG 标注点转输入时减去半个画幅，不做镜像。
投影公式：`projected.center = origin + direction * scale * author_scale * author.center`，
`projected.radius = scale * author_scale * author.radius`。

### 每模型每条件检查

1. 运行 `nekomote_session_probe`（无事件）获取投影后 `touch_regions`。
2. 对每个有反应的区域（bust→eye→face→head→pant 优先级）：
   - 中心点 touch（独立初态）：应命中某区域并返回反应。
   - 边界内点 touch（`center + radius * 0.3`，独立初态）：应命中。
3. 全局外点 touch（`[99999, 99999]`，独立初态）：不应触发任何反应。
4. 记录实际命中的区域和反应，不把 Busy/暂停误记为几何 miss。

### 状态分类

| 状态 | 含义 | 数量 |
| --- | --- | --- |
| OK | 全部条件全部区域中心/内点命中、外点不触发 | 180 |
| CPU_REJECTED | session_probe 编译或求值失败，沿用冻结账本 | 15 |
| MISSING_MODEL | model.nkm.json 不存在 | 0 |
| MISSING_SESSION | session.nks.json 不存在 | 0 |
| NO_ACTIVE_ZONES | 无区域或无反应 | 0 |

## 15 项 CPU_REJECTED

这些模型 session_probe 返回 `session=6 playback=3 geometry=5`，是
CPU 求值/编译失败，不是触摸逻辑问题。沿用冻结账本，不修复（C5 范围）：

```
azuki-maid-b, azuki-maid-b-custard, azuki-santa-b, azuki-wintermaid-b,
cinnamon-maid, coconut-dress, coconut-dress-a, coconut-hawaii-a,
coconut-maid, coconut-maid-a, coconut-maid-a-4yo, coconut-santa,
coconut-swimsuit-a, coconut-wintermaid, coconut-yukata
```

## Fraise B 先行证据包

`fraise-france-maid-b`（mirror=True，5 区域全有反应）4 组条件全部通过：

| 条件 | bust | eye | face | head | pant | 外点 |
| --- | --- | --- | --- | --- | --- | --- |
| default_idle | b02 ✓ | blink ✓ | h06 ✓ | h02 ✓ | u05 ✓ | miss ✓ |
| idle_120 | b02 ✓ | blink ✓ | h06 ✓ | h02 ✓ | u05 ✓ | miss ✓ |
| moved | b02 ✓ | blink ✓ | h06 ✓ | h02 ✓ | u05 ✓ | miss ✓ |
| scaled_half | b02 ✓ | blink ✓ | h06 ✓ | h02 ✓ | u05 ✓ | miss ✓ |

覆盖图：`captures/fraise-france-maid-b/{default_idle,idle_120,moved,scaled_half}.png`。
颜色：胸红、眼黄、脸绿、头青、下身紫。head 中心点落在 face 区域内，
按优先级触发 face 反应 h02——这是正确行为，不是缺陷。

## 合成契约测试（17 项）

| 类 | 测试 | 说明 |
| --- | --- | --- |
| Structure | test_total_models | 195 项 |
| Structure | test_freeze_fields | 冻结信息完整 |
| Structure | test_status_counts | 状态分布合理 |
| Structure | test_condition_names | 4 组条件名 |
| Structure | test_each_model_has_required_fields | 每项必需字段 |
| Structure | test_ok_models_have_conditions | OK 模型 4 条件 |
| Structure | test_cpu_rejected_have_no_conditions | CPU 拒绝无条件 |
| Invariants | test_outside_point_never_triggers | 外点不触发 |
| Invariants | test_ok_model_center_hits | OK 中心点命中 |
| Invariants | test_mirror_and_non_mirror_exist | 镜像/非镜像都存在 |
| Invariants | test_projected_regions_match_author_transform | 投影公式一致 |
| Negatives | test_missing_model_dir | 缺失模型→MISSING_MODEL |
| Negatives | test_find_zone_of_reaction | 反应→区域映射 |
| Negatives | test_inside_point_within_ellipse | 内点在椭圆内 |
| Negatives | test_outside_point_beyond_ellipse | 外点在椭圆外 |
| Negatives | test_zone_order_is_priority | 优先级顺序 |
| Negatives | test_conditions_cover_required_cases | 条件覆盖 |

## 工具用法

```sh
# 单模型审计
python3 tools/touch_audit/run_touch_audit.py \
  --assets assets \
  --probe ./build/release/bin/nekomote_session_probe \
  --output build/evidence/g12-touch-audit \
  --model fraise-france-maid-b

# 全量审计（约 15 分钟）
python3 tools/touch_audit/run_touch_audit.py \
  --assets assets \
  --probe ./build/release/bin/nekomote_session_probe \
  --output build/evidence/g12-touch-audit \
  --report build/evidence/g12-touch-audit/ledger.json

# 带覆盖图（需要 display 和 player）
python3 tools/touch_audit/run_touch_audit.py \
  --assets assets \
  --probe ./build/release/bin/nekomote_session_probe \
  --player ./build/release/bin/nekomote_player \
  --output build/evidence/g12-touch-audit \
  --model fraise-france-maid-b --capture
```

## 范围与限制

- 不改核心/反应数据、assets、外部配置或反应规则。
- 不向系统注入键鼠，不按窗口标题找窗口。
- 覆盖图仅为 Fraise B 生成（4 张 PNG）；全 195 模型覆盖图交 V9 视觉核对。
- 15 项 CPU_REJECTED 沿用冻结账本，不重跑。
- 来源一致、命中检查和目视吻合分别统计；本账本为来源+命中检查，
  目视吻合交 Gemini V9。
- 未进行图集或 CPU 全目录长跑；未改 assets 或外部配置。
