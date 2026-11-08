# 曲面祖先链：复现与原始数据

结论见 [报告](../../curve-chain-optimization-2026-11-08.md)。本轮只改变祖先访问路径，
没有采用此前隔离试验中的 SIMD 或重排求和候选。

## 基线与构建

基线 `0dc1466`，同一 GCC 15.2.1、Release/O3/LTO/Werror。
`environment.json` 保存二进制、生产代码、探针与实际资产哈希。
真实素材、二进制和帧文件不入库；本机临时材料在 `/tmp/nekomote-curve-chain-review/`。

在基线 checkout 和最终 checkout 分别构建，再使用同一探针链接各自的库：

```bash
cmake --preset release -DNEKOMOTE_BUILD_TESTS=ON -DNEKOMOTE_BUILD_ABI=ON
cmake --build --preset release -j6
c++ -std=c++20 -O3 -flto -Iinclude \
  docs/validation/compound-review-2026-11-08/runtime_probe.cpp \
  -Wl,--start-group build/release/lib/*.a -Wl,--end-group \
  -lpng -lz -lpulse -lfreetype -lfontconfig -lvulkan -ldl -lpthread -lm \
  -o /tmp/runtime-probe
taskset -c 0 /tmp/runtime-probe "$PWD/assets" 600 /tmp/runtime-frames
```

基线探针在修改代码前冻结，候选探针在修改 runtime 后链接；两者随后保持不变。
CPU 0 为本机允许核，复现时选择实际可用核。以上配置用于独立 checkout；
不要重配正在使用的构建目录而意外关闭其已有适配器设置。

## 采样和汇总

沿用上一轮提交的 `runtime_probe.cpp`，参数、种子、预热、字段序列化和计时范围不变。
顺序：baseline-0 → candidate-0 → candidate-1 → baseline-1 → baseline-2 → candidate-2。
每次运行遍历 7 型号 × 2 输入，每组预热 120 帧、600 个样本，固定步长 1/60 秒。
本轮采样与构建、测试、其他性能探针没有并行执行；没有修改电源策略或 CPU 频率。

```bash
python3 docs/validation/curve-chain-2026-11-08/analyze.py > /tmp/curve-summary.json
ctest --test-dir build/release --output-on-failure -j4 -LE gpu
ctest --test-dir build/release --output-on-failure -j1 -L gpu
```

`baseline-N.jsonl` / `candidate-N.jsonl` 是完整原始输出；`summary.json` 可由分析脚本重建。
脚本检查所有轮次字段 fingerprint 一致及预热后普通 C++ new 为零。
指纹覆盖全部 600 个采样帧；`frame-comparison.json` 保存首轮每 60 帧导出的逐字段
二进制比较结果和 SHA256，14 文件各含 10 帧，共 140 帧，不含 padding 或 producer 地址。

Session 的 advance/prepare 是直接计时；独立 Geometry 的 evaluate/build 使用相同最终
参数，但其缓存与调用范围不同，不能作为 Session 内嵌阶段的精确拆分。
没有 GPU 读回、UI、声音设备线程或桌面呈现在性能计时区间内。

`cpu-tests.txt`、`gpu-tests.txt` 与 `asan-tests.txt` 分别保存本轮验收输出。
ASan 使用独立二进制和依赖构建目录，只借用相同版本的依赖源码，避免污染 Release 库；
sanitizer 测量不参与性能结论。未做全型号动态、ARM64 或真实桌面长期认证。
