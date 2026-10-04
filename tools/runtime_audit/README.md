# 全目录 CPU 覆盖

`nekomote_runtime_audit` 是离线检查入口，依赖 importer/asset_io/runtime 和私有 JSON 报告库，
不依赖窗口、图形或声音。它直接导入显式源 JSON/resx，编译真实物理播放程序，逐步构帧；
不生成模型副本、不复制图集。初态和每条时间线使用独立 reset、seed=1，主动作 Replace、
差分 AdditiveParallel。默认每组120作者步（60Hz）；长动作剩余区间未被这次采样覆盖。

```sh
build/release/bin/nekomote_runtime_audit MODEL.json MODEL.resx.json \
  --output cpu-report.json --steps 120
python3 tools/import_emote/audit_catalog.py --reference-root ../NekoWebShow \
  --importer build/release/bin/nekomote_import \
  --runtime-auditor build/release/bin/nekomote_runtime_audit \
  --runtime-steps 120 --output build/evidence/runtime-catalog.json
```

单模型工具接受 `--resource-root DIR` 和显式 `--texture name=relative-path`；
目录工具从已有资源建立该映射。CPU失败返回2，参数/报告I/O错误返回1。
报告保留编译来源、几何节点路径、控制器索引、失败作者步和已采样计数，
不捕获后返回空帧成功。unsupported 时间线仍逐条列出，未选分支可能存在其它能力缺口。

目录工具启动时冻结两个二进制到自己的临时目录，记录SHA256；后续工作区构建不影响结果。
每型号结束后原子保存 `complete:false` 检查点，全部结束才标记 `complete:true`。
`structural_imports`、`runtime_compiled` 和 `runtime_cases_passed` 是不同检查层；
`playback_certified`/`visual_certified` 始终为0/false。必须另用原WebGL固定输入的实际
顶点、标量和截图完成画面/行为验收，不能用此报告代替。
