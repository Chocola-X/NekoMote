# P3 离线语义实验

`probe.py` 只消费项目当前资产，输出候选场景几何和诊断。正式预览/运行时不接受它的输出。`compare_geometry.py` 在原网页实际使用的 UV 上计算候选位置并比较顶点；不会回放或导入原引擎的几何。

```sh
python3 tools/pose_probe/probe.py \
  --asset build/evidence/chocola-date-a-v2/model.nkm.json \
  --variables '{"head_LR":15,"head_UD":-20}' \
  --output build/evidence/pose-example.json
python3 tools/pose_probe/compare_geometry.py \
  --asset build/evidence/chocola-date-a-v2/model.nkm.json \
  --probe build/evidence/pose-example.json \
  --trace build/evidence/reference/head-lr-ud.trace.json \
  --output build/evidence/geometry-example.json
```

参考截图命令见 `../reference_capture/README.md`。当前对照限定单图集，参考上传顺序为 texture=2；其他上传布局需要显式扩展诊断映射，不能把缺采样视为通过。记录 `missing_primitives`，出现缺失即失败。位置比较阈值冻结为 0.001 像素，证据见 `docs/validation/p3-geometry.json`。

已验证主样例及三个不同型号：参数范围映射、null mesh 的单位形状、规范化的 4×4 Bézier、同域 combine=1 的位移叠加、子到父的独立域、成对变量 type=0 限幅。作用域和具体输入见 `docs/semantics.md`。

实验仍忽略遮罩，均输出 `certified:false`；均匀细分网格只用于查看，不能认为匹配了原引擎的网格划分。它还不能保证部分继承、zoom/flip、cc、任意动态时间线、物理和全部字段。遇到非零的未验证成对控制会拒绝实验。`--disable-clamp` 只用于匹配参考的显式诊断 fixture。

全部记录可用 `validate.py --reference-root ../NekoWebShow --importer build/dev/bin/nekomote_import --browser /opt/chrome-linux/chrome --output <新证据目录>` 重做。浏览器路径用本机实际路径替换；先按参考采样文档安装 Playwright。工具创建新资产、采样原 PSB、检查参考源码 hash、运行候选公式并核对阈值/部件数量；每组完成就写报告，失败立即保留复现并退出。

原生诊断 CLI nekomote_pose 调用实际 C++ 场景，比较解析采样位置及生成网格的 UV、位置、三角形：

    build/dev/bin/nekomote_pose --asset <资产> --variables '<JSON>' \
      --sample-trace <参考.trace.json> --output <报告>

trace 只用于诊断验证，不是预览或运行时输入。--benchmark 2000 测量 evaluate+build_frame；不含 GPU。

完整复现可给 validate.py 增加 --native-evaluator build/dev/bin/nekomote_pose，重新捕获原网页并核对原生网格/拓扑。Python probe 的均匀网格仍只是早期独立公式实验，实际原生网格证据以 [p3-native-geometry.json](../../docs/validation/p3-native-geometry.json) 为准。
