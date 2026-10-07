# G10 交接：刷新 195 项 CPU/来源账本

> Codex 复核与后续修补以 [当前整合摘要](review-agents-current.md) 为准；下文保留本任务交付时的验证范围。

Agent: CodeArts / GLM-5.2
基线提交：`495dcc3`
完成提交：本文档同批
范围：`tools/import_all_assets.py`、`tests/reference_capture/test_import_all_assets.py`、`docs/g10-handoff.md`（新）
文件边界：目录工具、Python 测试、新 g10-handoff；不改 runtime/render/application/ABI/UI

## 任务

G7 的 171 CPU OK / 23 FAIL 是 C5 bm19/attr2 映射交付前的历史统计，不能直接复用为当前结果。G10 用当前 Release runtime_audit、统一 catalog_index 和新输出目录重新生成全部 195 条结构/CPU 账本，记录提交、工具/hash、schema、source/resx/PNG/会话来源、每项命令与失败详情。

## 交付内容

### 1. 溯源元数据

`import_all_assets.py` 账本顶层新增字段：

- `head_commit` — 当前仓库 HEAD commit hash（`git rev-parse HEAD`），不在 git 仓库中时为 null
- `generated_at` — ISO 8601 UTC 时间戳
- `tool_hashes` — `importer`/`runtime_auditor`/`session_tool` 的 sha256，缺失工具为 null
- `runtime_steps` — CPU 审计步数

检查点也写入这些字段，中断后可溯源。

### 2. 纯 CLI 解析测试

`test_import_all_assets.py` 新增 `CliParseTest`（7 项），不需要真实素材或 GPU：

- `--runtime-steps` 范围校验：0 和 36001 拒绝，退出非零
- 缺 catalog 拒绝，退出非零
- 缺 importer 拒绝，退出非零
- `tool_hash` 对 None/缺失路径返回 None，对文件返回 64 字符 sha256
- `head_commit` 返回 40 字符 hex 或 None

缺素材环境的纯 CLI 解析测试执行通过，只有真实 GPU/素材检查 Skipped，不把整类跳过误报为测试通过。

### 3. 真实 195 条账本

```sh
python3 tools/import_all_assets.py \
  --reference-root /run/media/chocola/DATA/Project/NekoWebShow \
  --importer build/release/bin/nekomote_import \
  --output /tmp/g10-ledger-output \
  --session-tool tools/import_session/import_session.py \
  --presentation /run/media/chocola/DATA/Project/NekoWebShow/config/model-presentation.json \
  --runtime-auditor build/release/bin/nekomote_runtime_audit \
  --runtime-steps 120 \
  --report /tmp/g10-ledger.json
```

结果（`/tmp/g10-ledger.json`）：

| 字段 | 值 |
| --- | --- |
| `complete` | true |
| `catalog_records` | 195 |
| `head_commit` | `495dcc3d2c325bd71c0856b0064b32c5557755fc` |
| `generated_at` | `2026-10-07T13:53:18+00:00` |
| `runtime_steps` | 120 |
| `tool_hashes.importer` | `3866ceb0894b6109...` |
| `tool_hashes.runtime_auditor` | `81d71da9396b96e4...` |
| `tool_hashes.session_tool` | `28cb078e8b55569f...` |
| `catalog_sha256` | `72bd4ad864c83d25...` |

统计：

| 维度 | 结果 |
| --- | --- |
| structure | OK 194, AMBIGUOUS 1 |
| cpu | OK 174, FAIL 20, PENDING 1 |
| visual | PENDING 195 |
| interaction | PENDING 195 |
| soak | PENDING 195 |

与 G7 对比：CPU OK 从 171 增至 174（C5 bm19/attr2 修复 3 项），FAIL 从 23 降至 20。

### 4. 账本字段完整性

OK 项 19 个字段：`bundle_sha256`/`character`/`commands`/`config`/`costume`/`cpu_detail`/`cpu_result`/`errors`/`external_textures`/`model`/`nodes`/`psb`/`psb_sha256`/`resx`/`resx_sha256`/`schema`/`source`/`source_sha256`/`status`/`variants`。

- OK 项都有 `schema`（=5）和 `bundle_sha256`（bundle 内全部文件 sha256 摘要）
- OK 项都有 `cpu_detail`（command/returncode/stdout_tail/stderr_tail）和 `cpu_result`（runtime_audit JSON）
- CPU FAIL 项都有 `cpu_detail`，保留 returncode 和 stdout/stderr 尾部
- AMBIGUOUS 项（`fraise-france-maid-b`）列出 `alternatives`（两个候选路径），不擅自挑选

### 5. 20 个 CPU FAIL 模型

```
azuki-maid-b, azuki-maid-b-custard, azuki-santa-b, azuki-wintermaid-b,
cinnamon-maid, coconut-dress, coconut-dress-a, coconut-hawaii-a,
coconut-maid, coconut-maid-a, coconut-maid-a-4yo, coconut-santa,
coconut-swimsuit-a, coconut-wintermaid, coconut-yukata,
fraise-casual-b, fraise-hawaii-b, fraise-maid-b, fraise-pajama-b, fraise-swimsuit-b
```

这些属于已知运行时缺口（bm19/attr2 之外的求值语义），非导入缺陷，交 Codex C5 继续。

### 6. 1 个 AMBIGUOUS 模型

`fraise-france-maid-b`：源目录有两个候选（`fraise-france-maid-b.json` 和 `fraise-france-maid-b.pure.json`），是已知数据损坏，非运行时缺陷。账本仅列候选/来源 hash，不擅自挑选文件或改核心让全绿。

### 7. 重复执行拒绝覆盖

对已有 `/tmp/g10-ledger-output` 再次运行：

- 194 项 `EXISTS_UNCHECKED`，1 项 `AMBIGUOUS`，退出非零
- 不调用导入器，CPU 全部 PENDING
- 额外文件 `extra-file.txt` 保留不变

### 8. 原素材保护

- 源素材路径只读，hash 已记录
- 原 G7 输出 `/tmp/g7-smoke-output` 保留未动
- 未修改外部仓库

## 验证

### Python 单元测试

```
python3 -m unittest discover -s tests/reference_capture -v
Ran 42 tests in 0.236s
OK
```

42/42 通过（原 35 + 新增 7 项 CliParseTest）。

### 真实 195 条账本

- `complete: true`，`catalog_records: 195`
- `head_commit`/`tool_hashes`/`generated_at`/`runtime_steps` 已记录
- structure 194 OK + 1 AMBIGUOUS
- cpu 174 OK + 20 FAIL + 1 PENDING
- visual/interaction/soak 全部 PENDING — 不宣称视觉或交互通过

### 重复执行拒绝

- 195 项 EXISTS_UNCHECKED/AMBIGUOUS，退出非零
- 额外文件保留不变

## 未完成 / 交接

- 20 条 CPU FAIL 属于已知运行时缺口（bm19/attr2 之外的求值语义），非导入缺陷，交 Codex C5
- 1 条 AMBIGUOUS（fraise-france-maid-b）是源数据多候选，非运行时缺陷
- visual/interaction/soak 全部 PENDING，需 Gemini V7 视觉证据和 Codex C6 交互验收
- 本轮不宣称 195 条视觉或交互通过，仅交付结构/CPU 账本
- 不改当前摘要/路线图宣称整模型通过
