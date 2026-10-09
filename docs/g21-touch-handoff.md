# G21：真实触摸/来源证据交付

基线：`4b9fedc`（G20+V13）；执行：CodeArts / GLM-5.2；证据目录：`build/evidence/g21-touch/`。依据 [AGENTS.md](../AGENTS.md) 与 [下一批交接](agent-next-handoff.md) G21 段，本轮建立独立 source-field 参考、补齐 moved/scaled_half 条件真实 request/response、补测 X250 等未执行边缘点。

## 1. 独立 source-field 参考

不复制导入 session/G13 区域，直接从原始 E-mote JSON 源字段重新计算触摸区域，验证与 `session.nks.json` 中 `interaction.regions` 一致。

**来源链**：
```
metadata.charaProfile.pixelMarker (eye, mouth, bust, bottom)
  + model-presentation.json (cutoffY, touchMarkers)
  → import_session.py 几何推导 + author 精修覆盖
  → interaction.regions (center, radius)
  → projected_regions (origin, mirror, scale)
  → PNG 坐标 (centered + [W/2, H/2])
```

**转换公式**（`tools/import_session/import_session.py:91-111`）：
- `size = max(60, |mouth - eye|)`
- `bust = markers.get('bust', mouth + size*2.5)`
- `waist = bust + (cutoffY - bust) * 0.55`
- `eye  = ellipse(0, eye, size*0.5, size*0.35)`
- `face = ellipse(0, (eye+mouth)/2, size*0.9, size*0.8)`
- `head = ellipse(0, eye - size*0.9, size*1.6, size*1.7)`
- `bust = ellipse(0, bust, size*1.3, size*1.0)`
- `pant = ellipse(0, waist, size*2.0, size*2.5)`
- author 精修（仅 head/bust/pant）：`center = ((ax+bx)/2, (ay+by)/2)`, `radius = (max(30, |bx-ax|/2), max(30, |by-ay|/2))`

**投影公式**（`run_touch_audit.py:87-101`）：
- `scale = cond.scale * author_scale`
- `direction = -1 if mirror else 1`
- `center = [origin[0] + direction * scale * author.center[0], origin[1] + scale * author.center[1]]`
- `radius = [scale * author.radius[i]]`
- `PNG = [centered[0] + width/2, centered[1] + height/2]`

**验证结果**：三模型×5 区域全部 MATCH（误差 < 1e-6）。

| 模型 | mirror | author_scale | pixelMarker 来源 | touchMarkers | cutoffY | 区域匹配 |
| --- | --- | --- | --- | --- | --- | --- |
| fraise-france-maid-b | True | 1.0 | eye=-384, mouth=-282.5, bust=-42.5, bottom=1250 | {} (无) | 1134.55 | 5/5 |
| cinnamon-maid-b | False | 1.0 | eye=-394, mouth=-292, bust=-28, bottom=1085 | {} (无) | 1040.73 | 5/5 |
| coconut-santa-b | False | 1.0 | eye=-401, mouth=-291, bust=38, bottom=1212 | {} (无) | 1167.27 | 5/5 |

注：三模型均无 touchMarkers（`-b` 变体不继承 `-a`），全部走 pixelMarker 几何推导。

## 2. moved/scaled_half 条件真实 request/response

G18 的 39 点仅覆盖 default_idle 和 idle_120。本轮为 moved（origin=[80,400], scale=1.0）和 scaled_half（origin=[0,400], scale=0.5）条件生成 30 个 spec 并运行冻结 validator。

**每模型×条件×区域**：bust_center, eye_center, face_center, head_center, bg_miss（共 5 点×3 模型×2 条件 = 30 点）。

**结果**：30/30 OK。

| 模型 | moved | scaled_half |
| --- | --- | --- |
| fraise-france-maid-b | 5/5 OK | 5/5 OK |
| cinnamon-maid-b | 5/5 OK | 5/5 OK |
| coconut-santa-b | 5/5 OK | 5/5 OK |

cinnamon head_center 实际触发 `cinnamon/h08`（非 h02），因 reaction_seed=1 在 cinnamon 的 9 个 head 反应中确定性选择 h08。修正 prefix 后通过。

## 3. X250 等未执行边缘点补测

V12 报告指出"候选X250点未执行"。本轮为三模型×default_idle 补测 12 个边缘点：

| 点类型 | PNG 坐标 | 几何判定 | 结果 |
| --- | --- | --- | --- |
| X250 bust 边缘 | [250, bust_y] | miss (bust 外) | 3/3 OK |
| X300 bust 边缘 | [300, bust_y] | hit (bust 内) | 3/3 OK |
| X550 bust 边缘 | [550, bust_y] | miss (bust 外) | 3/3 OK |
| bust inside (30% 半径偏移) | [bust_cx±0.3rx, bust_cy±0.3ry] | hit (bust 内) | 3/3 OK |

**结果**：12/12 OK。

## 4. 固定工具 hash

| 文件 | SHA-256 | 字节 |
| --- | --- | --- |
| `tools/touch_audit/g18_point_validator.py` | `506bcfa0...` | 11385 |
| `tools/touch_audit/run_touch_audit.py` | `b2d6fc53...` | 21094 |
| `build/release/bin/nekomote_session_probe` | `8b2e877a...` | 742976 |
| `build/evidence/g13-touch-audit-2026-10-08/full-catalog/ledger.json` | `60153731...` | 10002030 |
| `tools/import_session/import_session.py` | `28cb078e...` | 9236 |

validator/probe/ledger hash 与 G18 冻结一致（`build/evidence/g18-reviewed-2026-10-09/summary.json`）。

## 5. 验证点汇总

| 类别 | 条件 | 点数 | OK | FAIL |
| --- | --- | --- | --- | --- |
| moved/scaled_half center+miss | moved | 15 | 15 | 0 |
| moved/scaled_half center+miss | scaled_half | 15 | 15 | 0 |
| X250/X300/X550 边缘 | default_idle | 9 | 9 | 0 |
| bust inside 偏移 | default_idle | 3 | 3 | 0 |
| **合计** | | **42** | **42** | **0** |

所有点均有真实 request.json/response.json/probe 日志，保存在 `build/evidence/g21-touch/validated/<spec_name>/`。

## 6. 证据产物清单

- `build/evidence/g21-touch/source_field_reference.json`：三模型 source-field → session → projected → PNG 完整转换链
- `build/evidence/g21-touch/source-specs/*.spec.json`：42 个 spec 文件
- `build/evidence/g21-touch/validated/<spec_name>/`：每点 request.json, response.json, result.json, probe 日志
- `build/evidence/g21-touch/validated/summary.json`：30 点汇总（moved/scaled_half）

## 7. 说明与后续边界

- 本轮仅证明 CPU session 行为（区域投影、phase、reaction 前缀），**不认证视觉**。视觉观察由 Gemini V14 执行。
- 候选验证器保持 PENDING，不据椭圆数学或 prefix 宣布建议配置通过。
- Y 向下，作者→中心输出→PNG 公式一致，不反转 Y。Fraise 胸 centered=[0,357.5] → PNG=[400,897.5]。
- 原素材只读，未修改工具/公式/资产。GPU 共享锁未涉及（CPU only）。
- Gemini V14 需基于本轮证据包进行实际图像观察/测点，填写 version3 逐 condition 证据，区分可见/被遮挡/画幅外/未检查。
