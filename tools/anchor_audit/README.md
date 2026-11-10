# 物理锚点与动态画面对照

`nekomote_anchor_audit` 是无窗口、无声音设备、无配置写入的确定性正确性工具。
启用 `NEKOMOTE_BUILD_TOOLS=ON` 后构建该目标。使用真实资产时不会修改资产树；
输出仅为 JSONL，不保存模型或纹理。它不是性能基准，不应与性能采样同时运行。

```bash
build/release/bin/nekomote_anchor_audit "$PWD/assets" 120 timelines > /tmp/after.jsonl
python3 tools/anchor_audit/compare.py /tmp/before.jsonl /tmp/after.jsonl --steps 120
build/release/bin/nekomote_anchor_audit "$PWD/assets" 360 sessions > /tmp/session-after.jsonl
```

在基线与候选版本链接**同一份工具源码**，使用相同资产、步数和种子。
`compare.py` 对照每个案例的完成步数、错误类型/节点、帧/物理值指纹和 Controller
切换结果，缺失记录或不一致会失败。支持 `.jsonl.gz`；案例外层顺序可因分片而不同，
每个案例内部的帧顺序仍参与指纹。成功案例必须完成指定步数；原有拒绝案例单独统计，
不能作为已支持动作通过。工具本身成功退出只表示完成记录，不表示所有动作受支持。

- `timelines`（默认）：扫描 `characters/**/model.nkm`，每个模型从相同初态分别检查
  初态、拖动/缩放及每条主/差分时间线。主动作 Replace，差分 AdditiveParallel；
  中途暂停恢复，差分场景含 100ms 追赶输入。每步 hash 完整渲染字段和播放/物理变量。
  最后经共享 Controller 连续切换目录前 12 个型号，包含旧/新会话和渐变权重。
- `sessions`：对 date-a、两款 wintermaid、casual-b-no-bell、maple/yukata、
  coconut/casual、azuki/casual 共 7 款本地代表模型，检查每个已配置反应的主/差分组合、
  恢复动作、视线、拖动、缩放、音量快照、暂停、240Hz 分片及 500ms 停顿预算。
  通过 API 消费音频命令，不打开设备。随后按目录间隔选 12 款，额外插入 12 次
  未结束过渡中的切换，检查输出尺寸改变、宿主不可用恢复、拖动手势和 reset。
  工具只检查本地存在的代表型号；对比结果列出实际覆盖型号，缺素材不能算通过。

大资产集可运行独立分片，参数为从 0 开始的分片编号与分片总数：

```bash
build/release/bin/nekomote_anchor_audit "$PWD/assets" 120 timelines 0 2 > /tmp/shard0.jsonl
build/release/bin/nekomote_anchor_audit "$PWD/assets" 120 timelines 1 2 > /tmp/shard1.jsonl
cat /tmp/shard0.jsonl /tmp/shard1.jsonl > /tmp/after.jsonl
```

仅第 0 片运行 Controller 检查。各片可并发执行正确性审计，全部完成后再采样性能。
帧指纹明确序列化顶点/UV/颜色、索引、命令、混合、保护、遮罩及合成模式；不包含
结构 padding、缓存 revision 或 producer 地址。它能发现进入渲染器的动态数据变化，
不能代替全部 GPU 像素、真实桌面交互和长期运行验收。GPU 对照与基准复现见
[本轮证据说明](../../docs/validation/anchor-evaluation-2026-11-10/README.md)。
