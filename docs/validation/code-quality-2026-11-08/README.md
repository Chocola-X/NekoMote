# 性能复核定向证据

这是修复前的历史快照。最终修复结果见 [新证据](../code-quality-fixes-2026-11-08/README.md)，
不要将这里的旧输出与当前源码混为一份验收结果。

结论及范围见 [复核报告](../../code-quality-performance-audit-2026-11-08.md)。
这里没有真实素材副本；`assets` 参数是显式本地输入。探针只读资产、强制静音、禁用偏好保存；
使用独立 surfaceless EGL pbuffer，不发送系统输入、不创建用户桌面窗口。

从仓库根目录执行。需已有项目固定依赖、EGL/Mesa 及相关开发包；本次使用 GCC 15.2.1。

```bash
cmake --build --preset release -j 6
mkdir -p /tmp/nekomote-review
c++ -std=c++20 -O3 -flto -Iinclude \
  docs/validation/code-quality-2026-11-08/probe.cpp \
  -Wl,--start-group build/release/lib/*.a -Wl,--end-group \
  -lpng -lz -lEGL -lpulse -lfreetype -lfontconfig -lvulkan -ldl -lpthread -lm \
  -o /tmp/nekomote-review/probe
MESA_SHADER_CACHE_DISABLE=true LIBGL_ALWAYS_SOFTWARE=1 \
  /tmp/nekomote-review/probe "$PWD/assets" characters/chocola/date-a
MESA_SHADER_CACHE_DISABLE=true LIBGL_ALWAYS_SOFTWARE=1 \
  /tmp/nekomote-review/probe "$PWD/assets" characters/chocola/wintermaid
MESA_SHADER_CACHE_DISABLE=true LIBGL_ALWAYS_SOFTWARE=1 \
  /tmp/nekomote-review/probe "$PWD/assets" characters/maple/yukata

c++ -std=c++20 -O3 -flto -Iinclude \
  docs/validation/code-quality-2026-11-08/inventory.cpp \
  -Wl,--start-group build/release/lib/*.a -Wl,--end-group \
  -lpng -lz -ldl -lpthread -lm -o /tmp/nekomote-review/inventory
/tmp/nekomote-review/inventory assets/characters
python3 docs/validation/code-quality-2026-11-08/audio_inventory.py
```

`probe.cpp` 输出局部微基准、不同实例的 GPU 顶点复用错误、合成半透明像素差异、
共享 ABI 暂停脏状态及 Controller 配置副本问题。这是诊断程序，缺陷被观察到时仍输出结果，
退出码 0 只代表探针执行完成，**不代表这些行为已通过验收**。
其中 `invalid_composition_rejected=0` 仅用于观察门控确实短路；核心错误证据是两个有效实例
依次 prepare 后 GPU x 未更新，不能以修改受信帧后未校验单独推导安全缺陷。
像素使用 renderer 的诊断 capture 导出 straight RGBA，读回不在基准计时区间。

`inventory.cpp` 扫描 195 个模型的默认 geometry 初态，统计实际输出命令中的 protection/Multiply。
不覆盖 Session 的待机推进、动态资源分支及后续动画状态。
`audio_inventory.py` 只读 FLAC STREAMINFO，按 session 路径去重求和；263 是全资产唯一
FLAC 文件数，不能把全库总数当作单会话常驻量。PCM 结果不含 RMS、vector、设备缓冲及切换重叠。

`date-a.txt`、`wintermaid.txt`、`yukata.txt` 为最终探针原始输出；
`audio.txt` / `inventory.txt` 为资产清单结果；`source-hashes.txt` 固定待审代码内容。
`build.txt` 是完整 Release 构建完成后再次检查的 no-work 输出；本次首次构建完成 153 个步骤。
没有将其他 Agent 的 CTest 结果复制为本次独立测试证据。

两个薄壳也在同一固定依赖 Release/LTO/Werror 配置完成构建：

```bash
cmake --preset release -DFETCHCONTENT_UPDATES_DISCONNECTED=ON \
  -DNEKOMOTE_BUILD_X11=ON -DNEKOMOTE_BUILD_LAYER_SHELL=ON
cmake --build --preset release -j 6
```

配置/构建原始输出为 `configure-shells.txt` / `build-shells.txt`。
验证后将两个构建开关恢复 OFF。未运行 X11 / layer-shell 真实宿主。
