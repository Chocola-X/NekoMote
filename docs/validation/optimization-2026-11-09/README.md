# 2026-11-09 分阶段优化证据

结论见 [审查](../../optimization-audit-2026-11-09.md)，经验统一进入
[汇总](../../optimization-lessons.md)，不为每次失败另建经验文档。

## 最终合成 scissor（已撤回）

基线 `87ca4a5`；GCC15.2.1，Release/O3/LTO/Werror，Xeon E5-1680 v4，
RTX3080Ti / NVIDIA580.82.09，桌面 DISPLAY=:0 只用于建立离屏上下文，无系统输入。
`environment.json` 保存冻结二进制哈希。生产端唯一变量为 `rejected-scissor.patch`，
该补丁包含最终未保留的专项 Vulkan/GL GPU 测试；在基线可用 `git apply --check` 核验。
GPU 计时期间不运行构建或其他探针。未锁频或独占桌面，不把尾部噪声当确定因果。

`renderer_probe.cpp` / `vulkan_probe.cpp` 来自上一轮提交的探针，唯一调整是输出
3840×2160、原点1920/1200，角色缩放保持0.65。3真实型号，各待机/拖动，固定种子12345，
dt=1/60，120预热、300采样，CPU0，三轮前后/后前/前后交替。PNG导出在计时结束后进行。
每个 API 的6对最终PNG完全一致，哈希在 `scissor-summary.json`；不宣称所有动态帧图像已验证。

在基线 checkout 和应用撤回补丁的隔离 checkout 分别链接并冻结程序：

```bash
c++ -std=c++20 -O3 -flto -Iinclude \
  -Ibuild/release/gladsources/nekomote_gl_loader/include \
  docs/validation/optimization-2026-11-09/renderer_probe.cpp \
  -Wl,--start-group build/release/lib/*.a -Wl,--end-group \
  -lEGL -lpng -lz -lpulse -lfreetype -lfontconfig -lvulkan -ldl -lpthread -lm \
  -o /tmp/gl-probe
DISPLAY=:0 taskset -c 0 /tmp/gl-probe "$PWD/assets" desktop 300 /tmp/gl-images reuse
```

desktop 改 gles 可复现 GLES。Vulkan 链接命令见
[前轮说明](../compound-review-2026-11-08/README.md)，换成本目录的 vulkan_probe.cpp，
参数为 `assets 300 image-directory reuse`。Vulkan 隐藏宿主只用于设备/离屏提交，不呈现。
complete_us 含等本帧 GPU 完成；cpu_us 到提交；prepare_us 包含帧验证、上传与重复 resize 检查。
这些范围不是纯 GPU 时间或桌面 present 延迟。

## 关键帧区间缓存

基线仍为 `87ca4a5`（第一阶段只提交文档）；生产代码只改 GeometryInstance 的区间检索。
使用 [已提交的 runtime_probe](../compound-review-2026-11-08/runtime_probe.cpp)，
链接方法同前轮说明。7型号×2输入、120预热/600采样，CPU0，三轮前后/后前/前后。
`runtime-{before,after}-N.jsonl` 是原始数据，`runtime-summary.json` 是逐场景汇总；
`runtime-frames.json` 保存140份抽样帧的逐字段字节比较及哈希。实际帧与素材不入库。
Session 与独立 geometry 不是同一次内嵌调用的拆分；零普通new不扩展到驱动、UI或设备线程。

```bash
python3 docs/validation/optimization-2026-11-09/analyze.py > /tmp/recomputed.json
ctest --test-dir build/release --output-on-failure -j4 -LE gpu
ctest --test-dir build/release --output-on-failure -j1 -L gpu
```

汇总统一采用每场景每统计量的三轮中位数，再等权汇总变化率；无跨机器性能硬阈值。
没有把加载耗时、GPU读回、sanitizer开销混入稳态 Release 计时。

## 祖先重复验证

基线 `30e5ee2`，`validation-before-N.jsonl` 来自冻结的关键帧缓存版本，
`validation-after-N.jsonl` 只进一步修改 unsupported 检查。探针、参数与上节完全一致。
对应逐场景汇总与140帧比较在 `validation-summary.json`、`validation-frames.json`；
`environment.json` 保存实际源代码/二进制/资产哈希。`analyze.py` 也会重建本阶段聚合结果。
新增错误来源与恢复断言，既有 UI/ABI/GL/GLES/VK 路径继续运行最终回归。

`final-cpu.txt` / `final-gpu.txt` 为最终版本验收结果，`asan-tests.txt` 为 runtime
89 用例、28,637 断言的结果。ASan/UBSan 使用独立 Debug 构建：C++ 为 -g（O0），
第三方 FLAC 的 C 编译为 -g -O1，避免其默认 FORTIFY 在 O0 下不可用的诊断；
该依赖的上游 inline 提示产生 GCC15 的 Winline 提示，项目自身未新增警告或禁用规则。
实际源码只读共享 `build/release/_deps/*-src`，二进制依赖构建在隔离目录；
`ASAN_OPTIONS=detect_leaks=1`、`UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1`。
