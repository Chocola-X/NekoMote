# 2026-11-10 物理锚点按需求值证据

结论见 [报告](../../anchor-evaluation-2026-11-10.md)，后续边界维护在
[优化经验汇总](../../optimization-lessons.md)。基线 `ff74172`。
`environment.json` 冻结硬件/驱动/会话、源码、基线/最终二进制与 390 份模型/会话输入哈希。
未改资产、求值公式或 golden；没有系统输入注入、桌面配置修改或用户桌面安装。

## 构建和基线

Release/O3/LTO/Werror、GCC15.2.1、C++20。沿用现有 Release 配置，只打开
`NEKOMOTE_BUILD_TOOLS=ON`。初始基线 runtime 探针在修改生产代码前已链接冻结。
全模型和 GPU 诊断工具新增后，使用 `git show ff74172:...` 导出原版
`src/runtime/{geometry_scene,playback}.cpp` 及 `include/nekomote/runtime/geometry_scene.hpp`，
原头文件放独立 include overlay，编译两个原版对象，再放在当前静态库组之前链接基线。
这两份对象覆盖了本轮全部生产差异；application、physics 和渲染器源码没有改动。
`nm -C` 核查基线只包含无参数的旧 evaluate，最终版包含 GeometryEvaluation 参数版。

推荐复现时使用两个独立 checkout，相同编译选项，并用最终版的同一份探针源码链接。
也可使用上述旧对象方法。**编译基线探针时旧头文件 overlay 必须先于 -Iinclude**，
尤其 runtime_probe 直接调用 evaluate，不能让新签名意外拉入候选实现。
两个旧对象的编译需 `-Isrc/runtime -Ibuild/release/_deps/fast_float-src/include`。
不共享 ASan 与 Release 的依赖构建目录。

```bash
c++ -std=c++20 -O3 -flto -Iinclude \
  docs/validation/compound-review-2026-11-08/runtime_probe.cpp \
  -Wl,--start-group build/release/lib/*.a -Wl,--end-group \
  -lpng -lz -lpulse -lfreetype -lfontconfig -lvulkan -ldl -lpthread -lm \
  -o /tmp/runtime-final
```

## 全模型、动作与切换

[anchor_audit](../../../tools/anchor_audit/README.md) 新增为正式开发工具。
`timelines.jsonl.gz` 是基线的完整案例记录（无模型/图片）；`timeline-comparison.json`
记录原版与最终版逐案例等价检查，保留全部案例的 canonical SHA256。
候选按 6 个模型分片并发跑正确性，再由 compare.py 按案例键排序比较；每个案例内部的
帧序列、错误发生步数与物理值仍严格保序。第 0 片单独执行 Controller 检查。

`sessions.jsonl` 是基线应用层反应/切换记录，`session-comparison.json` 为前后比较。
7 款代表模型，每款初态及每个反应从一致初态运行 360 步；12 个目录间隔型号，
24 次装载/切换计数含首次安装，其中 12 次在上一过渡未结束时再次确认新模型。
记录 current/previous 帧及 fade_alpha；211 个 step 仍有 previous 会话。

```bash
build/release/bin/nekomote_anchor_audit "$PWD/assets" 120 timelines > /tmp/after.jsonl
python3 tools/anchor_audit/compare.py \
  docs/validation/anchor-evaluation-2026-11-10/timelines.jsonl.gz /tmp/after.jsonl --steps 120
build/release/bin/nekomote_anchor_audit "$PWD/assets" 360 sessions > /tmp/session-after.jsonl
python3 tools/anchor_audit/compare.py \
  docs/validation/anchor-evaluation-2026-11-10/sessions.jsonl /tmp/session-after.jsonl --steps 360
```

原有不支持的动作会记录错误，不算成功。当前记录的成功数量/错误数量以 comparison 为准。
这不是全动态无限状态认证；每条时间线仅取指定有限时段，所有动作组合也不是穷举。

## GPU 图像诊断

`renderer_probe.cpp` / `vulkan_probe.cpp` 从前轮现有探针扩展：3 型号 date-a、wintermaid、
yukata，4 工况（待机、拖动、首个反应、最后反应；移动工况同时含视线、缩放、暂停和
追赶），种子12345，121步，取第29/89/120步的4K图像。GL/GLES 用 NVIDIA EGL pbuffer，
Vulkan 用隐藏 GLFW 创建设备并离屏提交，均无系统窗口呈现或输入。
`gpu-images.json` 对照 108 对 PNG，全部文件字节相等，且全部有非零 alpha。
图片留在临时目录，不提交衍生真实素材。采样仅作正确性诊断，**不使用其单样本计时作性能结论**。

```bash
c++ -std=c++20 -O3 -flto -Iinclude \
  -Ibuild/release/gladsources/nekomote_gl_loader/include \
  docs/validation/anchor-evaluation-2026-11-10/renderer_probe.cpp \
  -Wl,--start-group build/release/lib/*.a -Wl,--end-group \
  -lEGL -lpng -lz -lpulse -lfreetype -lfontconfig -lvulkan -ldl -lpthread -lm -o /tmp/gl-audit
DISPLAY=:0 /tmp/gl-audit "$PWD/assets" desktop 1 /tmp/gl-images reuse
```

将 desktop 改 gles；Vulkan 参照前轮链接命令，换用本目录的 vulkan_probe.cpp，参数为
`assets 1 image-directory reuse`。基线加入两个旧对象及旧头 overlay，其余参数相同。

dependency_probe.cpp 只检查编译后节点数量和 partial 后可观察节点；
`dependencies.jsonl` 记录195款的闭包规模，不作计时。bitmap_bytes 是本机 libstdc++
按64位块取整的位图空间，vector_header_bytes 来自 sizeof，不含分配器元数据。

## 内联倒退与处置

initial-{baseline,candidate}-N.jsonl / initial-summary.json 保留首版三轮。只做部分求值
时，GCC/LTO把原本独立调用的 prepared-patch 多项式内联进逐顶点循环，完整帧构建变慢。
fresh-baseline-check.jsonl 是同选项重编原版后的额外14组检查，复现原成本。
compiler-layout.json 保存基线、首版、最终版的build_frame大小和对应调用证据。
kernel-boundary.patch 是首版到最终版的唯一生产增量，局部固定调用边界，继续调用共享
公式；不改变数学运算和浮点保存。其后重新采集最终全部动态/图像/性能与回归。

## 性能与回归

所有正确性审计/构建完成后才启动性能采样。7型号×待机/拖动视线、CPU0、固定1/60秒，
120步预热、600步采样，三轮顺序为前后/后前/前后。使用未修改的前轮 runtime_probe，
普通 new 计数范围和抽样序列化字段沿用其契约。每个指标先取每场景三轮中位数，再等权
汇总变化率；不能把独立 geometry 与嵌套 Session 的时间直接拼接成函数归因。
`baseline-N.jsonl` / `candidate-N.jsonl` 为原始数据，`runtime-summary.json` 为汇总。
`runtime-frames.json` 记录140份抽样帧的逐字段字节和SHA256比较；每组全部600帧均参与指纹。

```bash
python3 docs/validation/anchor-evaluation-2026-11-10/analyze.py
ctest --test-dir build/release --output-on-failure -j4 -LE gpu
ctest --test-dir build/release --output-on-failure -j1 -L gpu
```

cpu-tests.txt / gpu-tests.txt / runtime-tests.txt 为最终版本记录。CPU目录3项既有跳过没有
计为通过（原导入素材入口、Pulse集成、CJK字体）；本轮独立本地195模型审计另行统计。
ASan/UBSan 独立 Debug、C++ O0、依赖 FLAC C O1，runtime 分组和泄漏检测见 asan-tests.txt。
保留全节点的姿态与组合错误检查，部分模式无法 build_frame；后续完整模式重新构建状态。
