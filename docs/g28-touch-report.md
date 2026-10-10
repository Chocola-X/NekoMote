# G28：重建可消费的触摸证据

CodeArts / GLM-5.2，base `f562d23`（G27）。三型号×五条件重拍、冻结 manifest、合成负例和 Qt vs player 对比全部通过。交 Gemini V19 逐图观察。

## 交付

### 工具修正

- `generate_qt_condition_captures.py`：移除 `LD_LIBRARY_PATH`（G27 RPATH 已设），增加 probe stdout 诊断解析（`parse_capture_diagnostics`）和校验（`validate_capture`）：executed 步数、seed、rendered_frames。拒绝步数不匹配或帧未完成的捕获。
- `g18_prepare_evidence.py`：二进制路径 `build/release/bin/` → `build/plasma/bin/`，probe 改为 `nekomote_playback_probe`。
- `freeze_g24_evidence.py`：新增 4 类契约检查 — 步数诊断（拒绝旧步数日志/无 diagnostics）、捕获 ok（拒绝失败/非零 returncode/validation_errors）、PNG 有效性、下身入画（pant 区域与 alpha bbox 交集）。点绑定接受 >= 42 点。
- `generate_v9_ledger.py`：新增 `--g28-freeze` 参数和 `merge_g28_conditions` 函数，将 G28 freeze-manifest 的 `lower_body_in_frame` 条件合并到 G12 ledger。不删除任何现有 input/tool/binary/asset/png/hash/点绑定验证。
- `run_touch_audit.py`：`--model` 改为 `action="append"`，支持多型号一次运行。

### 新增工具

- `bind_g28_points.py`：从 G12 ledger 提取所有 touch probe 请求/响应 hash，绑定到 source-manifest。替代依赖已不存在 `g21-results-reviewed.json` 的 `bind_point_evidence.py`。
- `compare_qt_player.py`：用 `nekomote_player`（GLFW 路径）生成参照图，与 Qt/FBO probe clean 图计算像素 RMSE。player 是不同渲染路径，非自比。

### 证据（`build/evidence/g28-touch/`）

| 组件 | 结果 |
| --- | --- |
| source-manifest.json | 三型号来源 pixelMarker/presentation 重算与 session 匹配 |
| qt-captures/ | 15 clean 图（3型号×5条件），executed/seed/rendered_frames 全部校验通过 |
| overlays/ | 15 overlay 图（触摸区域椭圆叠加） |
| g12-ledger/ | 三型号 G12 审计 OK，132 点 CPU 证据 |
| point-binding.json | 132 点绑定，model/session/request/response hash 全部验证 |
| freeze-manifest.json | 69/69 契约检查通过 |
| qt-vs-player/ | 6 对比 OK（3型号×2条件），RMSE 0.011–0.015 |
| observation-template.json | 空 V19 观察模板（version 3，三型号×五条件×五区域） |

### 测试

- `test_g28_touch_audit.py`：30 新测试覆盖 `parse_capture_diagnostics`、`validate_capture`、`check_step_diagnostics`、`check_capture_ok`、`check_png_valid`、`check_lower_body_in_frame`、`merge_g28_conditions`、不一致 PNG、合并条件仍需证据。
- 全部 103 测试通过（36 touch_audit + 37 v9 + 30 g28）。

## 交 Gemini V19

证据包在 `build/evidence/g28-touch/`，观察模板在 `observation-template.json`。V19 需逐图检查五部位边界、镜像/坐标/透明遮罩及真实点击点，填写有效观察后运行 `generate_v9_ledger.py --g28-freeze` 证明被接受。

## 未完成

- WebGL 参照不可用（无浏览器环境），player 参照已替代；V19 可在有浏览器环境补充 WebGL 对比。
- V19 逐图视觉观察未完成（需 Gemini 执行）。
- 候选（candidates）仍为 PENDING_EVIDENCE，不应用。

Agent: CodeArts
Model: GLM-5.2
Context: docs/agent-workboard.md G28
