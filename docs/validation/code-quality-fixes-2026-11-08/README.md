# A 阶段修复证据与复现

结论见[修复验收](../../code-quality-performance-fixes-2026-11-08.md)。
以下命令从仓库根目录执行。使用已缓存的固定依赖，补充的 Catch2 是 v3.5.3
（commit `8ac8190e494a381072c89f5e161b92a08d98b37b`），源码位于 /tmp/nekomote-catch2。

```bash
cmake --preset release -DNEKOMOTE_BUILD_TESTS=ON \
  -DNEKOMOTE_BUILD_X11=ON -DNEKOMOTE_BUILD_LAYER_SHELL=ON \
  -DFETCHCONTENT_UPDATES_DISCONNECTED=ON \
  -DFETCHCONTENT_SOURCE_DIR_CATCH2=/tmp/nekomote-catch2
cmake --build --preset release -j 6
```

隔离测试使用 :88 Xvfb 21.1.18-2（1280x800x24、禁用 TCP），及
/tmp/nekomote-test-drivers/usr/lib/libvulkan_lvp.so（vulkan-swrast 1:25.2.3-2）。
两包取自 Arch archive，只解包到 /tmp；lvp.json 的 library_path 指向该绝对路径。
环境为 Mesa 25.2.3-2 / LLVM 20.1.8；不使用无设备可用的系统 NVIDIA ICD。

```bash
DISPLAY=:88 __GLX_VENDOR_LIBRARY_NAME=mesa LIBGL_ALWAYS_SOFTWARE=1 \
  MESA_SHADER_CACHE_DISABLE=true VK_ICD_FILENAMES=/tmp/nekomote-review/lvp.json \
  PULSE_SERVER=unix:/tmp/nekomote-review/no-pulse \
  XDG_CONFIG_HOME=/tmp/nekomote-review/config \
  ctest --test-dir build/release --output-on-failure -j 4 -LE gpu
DISPLAY=:88 __GLX_VENDOR_LIBRARY_NAME=mesa LIBGL_ALWAYS_SOFTWARE=1 \
  MESA_SHADER_CACHE_DISABLE=true VK_ICD_FILENAMES=/tmp/nekomote-review/lvp.json \
  PULSE_SERVER=unix:/tmp/nekomote-review/no-pulse \
  XDG_CONFIG_HOME=/tmp/nekomote-review/config \
  ctest --test-dir build/release --output-on-failure -j 1 -L gpu
```

`ctest-cpu.txt`：256 入口，254 执行通过、2 跳过。`ctest-gpu.txt`：4 入口全部通过，
含 GL 上传失败后重新准备的回归。构建、配置记录为 `build.txt/configure.txt`；
build 是最终小增量构建的输出，不把增量步骤数当作完整目标数量。

`probe.cpp` 使用独立 surfaceless EGL pbuffer、只读真实资产、强制静音并禁用配置保存，
不发送系统输入。与修复前历史探针不同，它断言不同 producer 上传正确、零版本验证生效、
展开面板可以静止、关闭时继续绘制且最终静止。可选第 3 个参数指定诊断 PNG 输出。

```bash
c++ -std=c++20 -O3 -flto -Iinclude \
  docs/validation/code-quality-fixes-2026-11-08/probe.cpp \
  -Wl,--start-group build/release/lib/*.a -Wl,--end-group \
  -lpng -lz -lEGL -lpulse -lfreetype -lfontconfig -lvulkan -ldl -lpthread -lm \
  -o /tmp/nekomote-review/fixed-probe
MESA_SHADER_CACHE_DISABLE=true LIBGL_ALWAYS_SOFTWARE=1 \
  /tmp/nekomote-review/fixed-probe "$PWD/assets" characters/chocola/date-a \
  /tmp/nekomote-review/chocola-date-a-fixed.png
```

另两模型为 characters/chocola/wintermaid 和 characters/maple/yukata。
每个模型编译预热 5、采样 40 次，prepare 预热 5、采样 1000 次，计时不包含 render/readback。
修复版重复 prepare P50 0.035–0.038µs，相同真实帧 revision=0 的完整 prepare P50
17.551–23.516µs；每次省 78,000–111,920 字节顶点/索引上传。
内存中会话编译少约 1.14–1.25ms，仅为局部重复编译模拟；非端到端加载或整机性能。
短窗口、无绑核，尾部波动仍保留在各模型文本输出中。

`*-baseline.txt` 的 baseline 只替换 renderer 翻译单元：

```bash
git show c86f469:src/render_gl/renderer.cpp > /tmp/nekomote-review/baseline-renderer.cpp
c++ -std=c++20 -O3 -flto -Iinclude -Isrc/render_gl \
  -Ibuild/release/gladsources/nekomote_gl_loader/include \
  -c /tmp/nekomote-review/baseline-renderer.cpp -o /tmp/nekomote-review/baseline-renderer.o
c++ -std=c++20 -O3 -flto -Iinclude \
  docs/validation/code-quality-fixes-2026-11-08/probe.cpp \
  /tmp/nekomote-review/baseline-renderer.o \
  -Wl,--start-group build/release/lib/*.a -Wl,--end-group \
  -lpng -lz -lEGL -lpulse -lfreetype -lfontconfig -lvulkan -ldl -lpthread -lm \
  -o /tmp/nekomote-review/baseline-probe
```

基线源码使用当前公共头编译，因此 Frame 的 C++ 调用布局一致，只有 renderer 实现不同；
不混用旧布局对象文件，也不称作两个完整 Git 版本的比较。
对三个模型运行该程序，保存相同 view（128×128、scale=.1）的 PNG 后逐字节比较，
三对完全一致；`images.json` 记录源 model.nkm 和 PNG hash，不把模型/截图素材加入版本控制。
合成半透明像素仍保持基线的两种 composition 差异，没有启用 direct-over 替换。

`source-hashes.txt` 固定被验证的实现和回归代码。
真实 GPU/桌面延迟、音频播放、长期运行和多屏不在此次证据范围内。
