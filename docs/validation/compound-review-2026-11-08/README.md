# 复合审查：可复现证据

结论与风险取舍见 [复核报告](../../compound-review-audit-2026-11-08.md)。
所有数据均来自本机，原报告的云端合成生成器未提供，不混用它的数值。

## 构建与探针

基线 `a8d2f4e`，GCC 15.2.1、C++20、Release/O3/LTO/Werror。
`build/release` 已启用 tests、tools、ABI、GL/Vulkan、X11/layer-shell；
直接重跑 configure release preset 会将 tests/tools 置 OFF，请显式保留这些设置。
固定依赖源码及 Catch2 3.5.3，具体环境、源/资产/二进制哈希见 `environment.json`。

```bash
cmake --build --preset release -j6
c++ -std=c++20 -O3 -flto -Iinclude \
  docs/validation/compound-review-2026-11-08/runtime_probe.cpp \
  -Wl,--start-group build/release/lib/*.a -Wl,--end-group \
  -lpng -lz -lpulse -lfreetype -lfontconfig -lvulkan -ldl -lpthread -lm \
  -o /tmp/runtime-probe
taskset -c 0 /tmp/runtime-probe "$PWD/assets" 600 /tmp/runtime-frames
```

在独立基线 checkout 用同一个 runtime_probe.cpp 和相同编译选项构建基线程序，
先冻结二进制，再修改生产代码。CPU 0 为本次允许的 CPU，复现应选择本机可用核。
每组先预热 120 帧、固定 1/60 秒和种子 12345，再测 600 帧；
原版→候选、候选→原版、原版→候选交替三轮，构建/测试不与性能采样并行。
计时不做像素读回，输入、字段 hash、抽样序列化和结果输出在计时之外。
没有锁 CPU/GPU 频率、修改电源模式或运行独占桌面，因此不把尾部波动归为确定因果。

- `baseline-N` / `preconverted-N`：最初直接扩大 State 的候选，对照原版。
- `baseline-pool-N` / `pooled-pool-N`：独立控制点数组的候选，对照原版。
- `baseline-final-N` / `final-final-N`：最终全部 runtime 改动，再次对照冻结原版。
- Session 的 advance/prepare 是直接计时；另外的 geometry_evaluate/build 是
  同参数的独立实例，缓存状态不同，不可直接拼接为 Session 的精确函数归因。
- ordinary C++ new 只在预热后的 Session advance/prepare 和独立几何路径中计数；
  不含输入调用、驱动 malloc、aligned new、其他线程或整个桌面应用。
- 每组全部 600 帧的字段 fingerprint 参与对比；每 60 帧导出一次，14 文件共
  140 份帧。仅序列化字段，无对象 padding、producer 地址或缓存版本。
  `comparison.json` 记录字节比较与 SHA256；真实素材及衍生二进制帧不入仓。

GL/GLES 探针链接时增加：

```bash
c++ -std=c++20 -O3 -flto -Iinclude \
  -Ibuild/release/gladsources/nekomote_gl_loader/include \
  docs/validation/compound-review-2026-11-08/renderer_probe.cpp \
  -Wl,--start-group build/release/lib/*.a -Wl,--end-group \
  -lEGL -lpng -lz -lpulse -lfreetype -lfontconfig -lvulkan -ldl -lpthread -lm \
  -o /tmp/renderer-probe
DISPLAY=:0 taskset -c 0 /tmp/renderer-probe "$PWD/assets" desktop 600 /tmp/gl-images reuse
# desktop 可改 gles；reuse 改 upload 强制每帧上传，其他代码完全相同。
```

GL/GLES 使用 NVIDIA EGL pbuffer，无可见窗口、无系统输入。
probe 包装 glBufferSubData 统计实际调用数/字节，保留全部验证。
`prepare_us` 含 prepare 与稳定尺寸 resize 检查；`cpu_us` 为会话更新到渲染提交，
`complete_us` 再包含本帧 fence 等待。GPU 单帧串行完成，没有桌面 present。
每组结束后才诊断截图，不计入性能；`images-*.png` 的 hash 比较见 comparison.json。
GL 探针冻结后补充了 reserve 报错失效保护，正常 reserve 之外的稳态路径未再改变。

Vulkan 探针用同样的输入与时序，私有隐藏 GLFW 窗口只用于取得 Vulkan surface。
`begin_frame(..., false)` 录制离屏帧，present() 仅 submit，不呈现系统窗口。
Release 不启用 validation layer；正式 GPU 正确性测试另行执行。

```bash
c++ -std=c++20 -O3 -flto -DNDEBUG -Iinclude -Iapps/player -Isrc/render_vk \
  -Ibuild/release/_deps/glfw-src/include \
  docs/validation/compound-review-2026-11-08/vulkan_probe.cpp \
  -Wl,--start-group build/release/lib/*.a -Wl,--end-group \
  -lEGL -lX11 -lpng -lz -lpulse -lfreetype -lfontconfig -lvulkan -ldl -lpthread -lm \
  -o /tmp/vulkan-probe
DISPLAY=:0 taskset -c 0 /tmp/vulkan-probe "$PWD/assets" 600 /tmp/vk-images reuse
```

`ebo-N-API-upload/reuse.jsonl` 是同一二进制切换 index_revision 的三轮原始数据。
EBO 收益以这种对照隔离，CPU runtime 收益以基线/最终程序对照隔离；二者不能相加
后宣称实际桌面帧率或功耗改善。Vulkan 没有截获 memcpy 计数，不冒充测过驱动上传次数。

```bash
python3 docs/validation/compound-review-2026-11-08/analyze.py > /tmp/summary.json
ctest --test-dir build/release --output-on-failure -j4 -LE gpu
```

GPU CTest 由仓库脚本内部串行加锁，使用私有 Xvfb、Mesa GL/GLES 和 Lavapipe，
结果保存于 gpu-tests.txt；不要在 ctest 外再锁同一个 gpu-validation.lock。
ASan/UBSan 使用 asan preset、独立二进制目录，`[runtime]` 分组及泄漏检查结果在
asan-tests.txt。原始构建日志保留于本次工作临时目录，交付保留最终验收结果。

summary.json 对每组每个指标先取三轮中位数，再汇总等权变化率，不按实际桌面使用
时长加权，不设置跨机器性能硬阈值。3 个跳过测试未记为通过。
