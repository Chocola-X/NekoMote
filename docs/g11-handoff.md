# G11 交接：195 项原始 PSB 图集与解包 PNG 来源审计

Agent: CodeArts / GLM-5.2
基线提交：`998a582`
完成提交：本轮
范围：`tools/reference_capture/audit_atlas_sources.py`、`tests/reference_capture/test_audit_atlas_sources.py`、`docs/validation/g11-atlas-source-ledger.json`、`docs/g11-handoff.md`
文件边界：离线目录/采样工具、Python 测试、新交接；不改 runtime/render/application/ABI

## 任务

G10 交付了 195 项结构/CPU 账本，但未审计浏览器实际上传的 RGBA8 图集字节与解包 PNG 是否一致。G11 在公共 `texImage2D` 上传边界记录实际 RGBA8 图集的尺寸、字节数和 SHA256，与解包 PNG 解码为 RGBA8 的字节核对，明确关联每个 PSB 上传与每个 resx 资源。

## 交付内容

### 1. 审计工具

`tools/reference_capture/audit_atlas_sources.py`：

- 用统一 `catalog_index` 保留全部 195 条目录记录
- 读取每个模型的 resx `Resources` 字段，定位全部解包 PNG
- 用 Pillow 解码每个 PNG 为 RGBA8，记录 width/height/byte_count/rgba_sha256/flipped_sha256
- 调用 `capture.py --trace-atlas-hashes` 在浏览器 `texImage2D` 上传边界记录实际 RGBA8 图集的尺寸/字节数/SHA256
- 按 rgba_sha256 关联每个上传与每个 resx 资源（同时检查正向和垂直翻转）
- 已有输出/报告拒绝覆盖，失败退出非零
- 检查点原子写入，中断后可溯源

### 2. 合成正/负例测试

`tests/reference_capture/test_audit_atlas_sources.py`（23 项，不需要真实浏览器/GPU/素材）：

- **PngDecodeTest**（4 项）：已知 RGBA8 解码、非对称图像翻转 hash 不同、对称图像翻转 hash 相同、缺失文件报错
- **ResxParseTest**（2 项）：Resources 解析为绝对路径、空 Resources 返回空 dict
- **AssociateAtlasTest**（9 项）：全匹配、翻转匹配、缺失上传、额外上传、双向不匹配、空集、无 PNG、无上传、多图集全匹配
- **AuditOneTest**（4 项）：上传 hash 等于 PNG 时 MATCH、捕获错误保留、缺失候选拒绝、缺失 resx 拒绝
- **CliParseTest**（4 项）：已有输出拒绝、已有报告拒绝、超时范围拒绝、缺失 catalog 拒绝

### 3. 真实 195 项来源账本

```sh
python3 tools/reference_capture/audit_atlas_sources.py \
  --reference-root /run/media/chocola/DATA/Project/NekoWebShow \
  --capture-tool tools/reference_capture/capture.py \
  --browser ~/.cache/ms-playwright/chromium-1194/chrome-linux/chrome \
  --output /tmp/g11-atlas-output \
  --report /tmp/g11-atlas-ledger.json \
  --timeout 120
```

结果（`docs/validation/g11-atlas-source-ledger.json`）：

| 字段 | 值 |
| --- | --- |
| `complete` | true |
| `catalog_records` | 195 |
| `head_commit` | `998a58270c9e9dbc9e19c58ce87facbdc8c224e9` |
| `generated_at` | `2026-10-08T00:59:00.765718+00:00` |
| `timeout` | 120 |

统计：

| 状态 | 数量 | 含义 |
| --- | --- | --- |
| MATCH | 167 | 每个上传图集的 RGBA8 SHA256 与某个解包 PNG 完全一致 |
| NO_PNG | 27 | resx Resources 为空，纹理嵌入 PSB（ExternalTextures=false），无外部 PNG 可审计 |
| MISMATCH | 1 | coconut-dress：上传 hash 与 PNG hash 不符（已知差异） |
| **总计** | **195** | |

### 4. NO_PNG 分析（27 项）

以下模型的 resx `Resources` 为空且 `ExternalTextures=false`，纹理以 `pixel` 数据嵌入 PSB 内部，无外部 PNG 文件可以审计：

```
azuki-casual, azuki-dress, azuki-maid, azuki-teenage,
chocola-casual, chocola-dress, chocola-koneko, chocola-lolita,
chocola-maid, chocola-pajama,
cinnamon-casual, cinnamon-dress, cinnamon-maid, cinnamon-teenage,
coconut-casual, coconut-koneko, coconut-maid, coconut-pajama,
maple-casual, maple-dress, maple-maid, maple-teenage,
vanilla-casual, vanilla-dress, vanilla-koneko, vanilla-maid, vanilla-pajama
```

这些模型在浏览器中上传的图集来自 PSB 内嵌纹理，而非外部 PNG。审计工具正确报告 NO_PNG，不伪造匹配。

### 5. MISMATCH 分析（1 项）

`coconut-dress`：

| 来源 | 尺寸 | RGBA8 SHA256 |
| --- | --- | --- |
| 解包 PNG | 4096×2048 | `83ee47245398adee79bd9c0a8bc57b821e92aba10f5f9ade8a5d1fae4d8c4302` |
| 浏览器上传 | 4096×2048 | `ca08b3f7697c88944e1e7d08f0d2983394546998064140ac5b7fc6f6e254969a` |

尺寸一致但内容不同。这是已知差异：外部 PNG 与官方上传不符（见 workboard G11 说明）。本轮隔离修正仅在项目内，不覆盖原 G10/C5 账本，不修改外部素材。

### 6. 账本字段完整性

MATCH 项字段：`atlas_uploads`/`capture_detail`/`character`/`commands`/`costume`/`errors`/`matches`/`model`/`png_resources`/`psb`/`psb_sha256`/`resx`/`resx_sha256`/`source`/`source_sha256`/`status`。

- 每项都有 `psb`/`psb_sha256`（PSB zip 路径和 hash）
- 每项都有 `source`/`source_sha256`（模型 JSON 路径和 hash）
- 每项都有 `resx`/`resx_sha256`（resx 路径和 hash）
- MATCH 项的 `png_resources` 每条含 `width`/`height`/`rgba_sha256`/`flipped_sha256`/`rgba_bytes`
- MATCH 项的 `atlas_uploads` 每条含 `width`/`height`/`bytes`/`rgba_sha256`
- MATCH 项的 `matches` 记录 png_index↔upload_index 关联和 orientation（normal/flipped）

### 7. 溯源元数据

账本顶层含 `head_commit`/`generated_at`/`tool_hashes`/`browser_path`/`timeout`，可复核工具版本和执行环境。

## 验证

### 合成正/负例测试

```
python3 -m unittest tests.reference_capture.test_audit_atlas_sources -v
Ran 23 tests in 0.014s
OK
```

23/23 通过。

### 真实 195 项账本

- `complete: true`，`catalog_records: 195`
- MATCH 167：浏览器上传的 RGBA8 字节与解包 PNG 完全一致
- NO_PNG 27：纹理嵌入 PSB，无外部 PNG 可审计
- MISMATCH 1：coconut-dress 已知差异，不修改外部素材

### 重复执行拒绝

已有输出/报告时拒绝覆盖，退出非零。

## 未完成 / 交接

- coconut-dress 的 PNG 与上传不符差异是已知问题，本轮不修改外部素材，交 Codex 在项目内处理
- NO_PNG 27 项的 PSB 内嵌纹理未审计（纹理在 PSB pixel 数据中，非外部 PNG），如需审计需要解码 PSB 内嵌纹理
- 图集一致不等于全模型视觉/交互/soak 通过
- 不重复 CPU 长跑，来源未改变
