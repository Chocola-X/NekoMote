# G7 交接：可靠目录账本与导入负例

> Codex复核与后续修补以 [当前整合摘要](review-agents-current.md) 为准；下文保留本任务交付时的验证范围。

Agent: CodeArts / GLM-5.2
基线提交：`658e3ae`
完成提交：本文档同批
范围：`tools/import_emote/catalog_index.py`（新）、`tools/import_all_assets.py`、`tools/import_all_test.py`、`tools/import_emote/audit_catalog.py`、`tests/reference_capture/test_import_all_assets.py`

## 交付内容

### 1. 共享 canonical 选择模块

新增 `tools/import_emote/catalog_index.py`，提取三个入口共用的逻辑：
- `load_catalog(reference_root)` — 加载 catalog 并返回 (dict, sha256)
- `build_index(reference_root)` — 索引所有解包模型 JSON，跳过 `.resx.json`，按 stem 去除 `.pure` 后缀分组
- `resolve_candidates(index, name)` — canonical 选择：优先父目录名等于 key 的候选；恰好一个时返回它，否则返回全部让调用方按歧义处理
- `find_external_textures(source, src_dir)` — 找 pixel==null 的外部纹理，歧义时返回 None
- `relative_to(path, root)` — 安全相对路径，跨设备时回退为 str(path)
- `sha256(path)` — 流式哈希

`import_all_assets.py`、`import_all_test.py`、`audit_catalog.py` 均改为 `import catalog_index`，消除三处 `build_index`/`resolve_candidates` 副本。`audit_catalog.py` 的 CPU 求值实现未改。

### 2. 195 行账本

`import_all_assets.py` 账本每项保留：
- `model`/`character`/`costume`/`variants`/`config` — catalog 元数据
- `status` — `{structure, cpu, visual, interaction, soak}` 五字段，未运行保持 `PENDING`
- `errors` — 失败原因列表
- `commands` — 执行的命令列表（导入器、会话工具、CPU 审计）
- `nodes` — 关键路径（source、target）
- `source`/`source_sha256`/`resx`/`resx_sha256`/`psb`/`psb_sha256` — 源素材哈希
- `external_textures` — 外部纹理映射
- `schema` — 已发布 bundle 的 model.nkm.json schema 版本
- `bundle_sha256` — bundle 内全部文件的 sha256 摘要
- `cpu_detail` — CPU 审计命令、返回码、stdout/stderr 尾部
- `cpu_result` — CPU 审计 JSON 结果
- `alternatives` — 歧义时的全部候选路径

`summary` 统计 `structure`/`cpu`/`visual`/`interaction`/`soak` 各状态计数，未运行保持 `PENDING` 不计入 OK。

### 3. 原子检查点

`save_checkpoint(report, path)` 先写 `.tmp` 再 `rename`，中断时不破坏上一次完整账本。每项完成后写检查点（`complete: false`），全部完成后写最终账本（`complete: true`）并删除检查点。

### 4. 失败/超时保留原因

- 导入器失败：`structure=FAIL`，保留 `commands`、`errors`（stderr 尾部），清理 staging
- 导入器超时：`structure=TIMEOUT`，保留 `commands`、`errors`（timeout 原因）
- model.nkm.json 缺失：`structure=FAIL`，保留原因
- 会话工具失败/超时：`structure=SESSION_FAIL`/`SESSION_TIMEOUT`
- CPU 审计失败/超时：`cpu=FAIL`/`TIMEOUT`，保留 `cpu_detail`
- 竞争创建目标：`structure=FAIL`，保留用户文件
- 已有目标：`structure=EXISTS_UNCHECKED`，不调用导入器

### 5. 索引每批只建立一次

原 `import_one` 在每项内部调用 `build_index`，195 项重复扫描整个 `model_conversion/`。修复为 `main` 预先建立 `index` 并传入 `import_one`。

### 6. 负例 fixture

`test_import_all_assets.py` 从 5 项扩展到 22 项，新增：
- `CandidateResolutionTest`：歧义拒绝、缺失拒绝、canonical 父目录名优先
- `ImportFailureRecordingTest`：导入失败/超时/manifest 缺失/会话失败保留命令与原因
- `CheckpointAndSummaryTest`：检查点原子写入、summary 五字段统计、成功导入记录 schema 和 bundle_hash
- `CatalogIndexTest`：catalog 缺失拒绝、resx 跳过、.pure 后缀剥离、纹理歧义返回 None、inline pixel 跳过、外部路径回退

## 验证

### Python 单元测试

```
python3 -m unittest discover -s tests/reference_capture -v
Ran 31 tests in 0.219s
OK
```

31/31 通过（原 14 + 新增 17）。

### 真实 195 条账本

```
python3 tools/import_all_assets.py \
  --reference-root /run/media/chocola/DATA/Project/NekoWebShow \
  --importer build/release/bin/nekomote_import \
  --output /tmp/g7-smoke-output \
  --session-tool tools/import_session/import_session.py \
  --presentation /run/media/chocola/DATA/Project/NekoWebShow/config/model-presentation.json \
  --runtime-auditor build/release/bin/nekomote_runtime_audit \
  --runtime-steps 30 \
  --report /tmp/g7-smoke-ledger.json
```

结果：
- `catalog_records: 195`，`complete: true`
- `structure: {OK: 194, AMBIGUOUS: 1}`（`fraise-france-maid-b` 已知数据损坏）
- `cpu: {OK: 171, FAIL: 23, PENDING: 1}`
- `visual/interaction/soak: {PENDING: 195}` — 不宣称视觉通过
- 每条都有 `commands`/`nodes`/`errors` 字段
- OK 项都有 `schema` 和 `bundle_sha256`
- CPU FAIL 项都有 `cpu_detail`
- `catalog_sha256` 已记录

### 重复执行拒绝覆盖

对已有 `/tmp/g7-smoke-output` 再次运行，全部 195 条 `EXISTS_UNCHECKED`，退出非零，不调用导入器。

### 原素材保护

源素材路径只读，hash 已记录（如 `chocola-date-a` source_sha256: `a6496ba68886430d...`）。未修改外部仓库。

## 未完成 / 交接

- 23 条 CPU FAIL 属于已知运行时缺口（bm19/attr2 等求值语义），非导入缺陷，交 Codex C5
- 1 条 AMBIGUOUS（fraise-france-maid-b）是源数据多候选，非运行时缺陷
- visual/interaction/soak 全部 PENDING，需 Gemini V6 视觉证据和 Codex C6 交互验收
- 本轮不宣称 195 条视觉或交互通过，仅交付结构/CPU 账本
