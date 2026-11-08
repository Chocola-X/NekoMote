# 剩余建议的分步对照证据

结论见 [报告](../../remaining-optimization-2026-11-08.md)。基线 `1846c37`。
这里保存诊断源码、文本结果和 CSV，不包含真实模型、字体、声音或截图。
真实素材通过显式 `assets` 路径只读取得；探针不写配置、不发送系统输入。
图像保留 RGBA 哈希与误差统计，原始 PNG 留在本次临时诊断目录。

## 内容

- `audio-[1-3]-{baseline,final}.txt`：交替三轮，旧/新设备同一私有 Pulse 服务。
- `ui-baseline-*.txt`、`text-final-*.txt`：单独 UI，每场景 200 帧预热、1000 样本；有真实 draw/上传/字节计数。
- `text-paired-*`：单独 UI，基线和文字剔除版同进程、相同字体、交替先后顺序，100 对预热、120 对样本、60Hz 调度；本批运行时绑到允许的首个 CPU。
- `whole-*`：wintermaid advance/prepare + GL 模型 prepare/render + UI，同一进程两份独立同种子会话，100 对预热、120 对样本、60Hz 调度，三轮，未绑核。
- `whole-summary.json`：完整帧各轮 CPU mean/P50/P95/P99，七状态等权汇总。CSV 含各帧 CPU/GPU 原值。
- `image-comparison.json`：14 对 UI、27 对 GLES atlas 画面的 RGBA 哈希、误差和可见 alpha 限制统计。
- `rejected-npot.patch`：已经撤回的 GLES 试验，**不是最终改动**。
- `atlas-*.txt`：实际裁切 atlas RGBA 容量及驱动环境，不是实际显存计量。
- `audio-tests.txt`、`audio-asan.txt`、`audio-disconnect.txt`、`audio-timeout.txt`：私有设备集成与生命周期结果。
- `cpu-final.txt`、`gpu-final.txt`、`ui-final-*.txt`、`build-final.txt`：最终代码验证。
- `environment.json`、`binary-hashes.txt`、`measured-source-hashes.txt`：基线/资产/生产代码/冻结探针哈希。
  随附探针为采样后经 clang-format 整理的同一逻辑，原始测量文件的字节哈希单独保存。

## 编译与运行

需当前项目已配置的 Release/LTO 构建及 EGL/Pulse 开发库。探针使用 EGL pbuffer，
默认 DISPLAY=:0 连接本机 NVIDIA 驱动但不创建可见窗口；无真实 EGL 设备时不要将软件结果混入同表。

```bash
cmake --build --preset release -j6
mkdir -p /tmp/nekomote-remaining
c++ -std=c++20 -O3 -flto -Iinclude \
  docs/validation/remaining-optimization-2026-11-08/audio_probe.cpp \
  build/release/lib/libnekomote_audio.a -lpulse -pthread \
  -o /tmp/nekomote-remaining/final-audio-probe
```

音频探针要求 `PULSE_SERVER` 是 `unix:/tmp/nekomote-remaining/` 下的**测试私有服务**。
本次从 Arch 包 `pulseaudio-17.0+r43+g3e2bb8a1e-1-x86_64.pkg.tar.zst` 解压运行，
包来源为 `https://archive.archlinux.org/packages/p/pulseaudio/`，未安装系统包。
私有进程使用 `-n --daemonize=no --exit-idle-time=-1 --use-pid-file=no --disable-shm`，
只加载 `module-native-protocol-unix` 和 `module-null-sink sink_name=nekomote_test rate=8000 channels=1`。
DBUS session/system 地址指向不存在的私有 socket，runtime/config/state 均在临时目录，
不复用用户服务、DBus 名称或物理声卡。确保私有服务器已经就绪后执行：

```bash
PULSE_SERVER=unix:/tmp/nekomote-remaining/pulse-runtime/native \
  /tmp/nekomote-remaining/final-audio-probe
NEKOMOTE_TEST_PULSE_SERVER=unix:/tmp/nekomote-remaining/pulse-runtime/native \
PULSE_SERVER=unix:/tmp/nekomote-remaining/pulse-runtime/native \
  ctest --test-dir build/release -R '^audio_device_(contract|pulse_integration)$' --output-on-failure
```

旧探针应从基线独立构建并冻结，不能拿当前 libaudio 重新链接后仍称 baseline。
`audio_probe ... disconnect` 在 Ready 后等待服务失败，用于停止测试私有 daemon 后的 join 验证。
`audio_timeout_probe.cpp` 针对只监听但不响应的私有 Unix socket，验证约 5 秒 deadline。
ASan/UBSan 直接编译 audio_probe + src/audio/device.cpp，`-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer`。

UI 单版本及 atlas 探针示例（将 `ui_probe` 换为 `atlas_probe` 即可）：

```bash
c++ -std=c++20 -O3 -flto -Iinclude \
  -Ibuild/release/gladsources/nekomote_gl_loader/include \
  docs/validation/remaining-optimization-2026-11-08/ui_probe.cpp \
  -Wl,--start-group build/release/lib/*.a -Wl,--end-group \
  -lpng -lz -lEGL -lpulse -lfreetype -lfontconfig -lvulkan -ldl -lpthread -lm \
  -o /tmp/nekomote-remaining/ui-probe
flock build/gpu-validation.lock /tmp/nekomote-remaining/ui-probe \
  "$PWD/assets" /tmp/nekomote-remaining/ui-result desktop capture
```

`ui_probe` 第三个参数为 desktop/gles；环境变量 `NEKOMOTE_BENCH_PACED=1` 改为 120 样本和 60Hz。
`atlas_probe` 同样接受 assets/output/api，固定输出三个真实模型的 27 张图；无计时结论。
NPOT 试验应在隔离 checkout 应用补丁，画面对照不通过即停止，不能将其遗留到正式树。

完整帧同进程对照需生成基线 UI 的独立命名空间：

```python
from pathlib import Path
import subprocess
root = Path('/tmp/nekomote-remaining')
for source, target in [('include/nekomote/render_gl/ui_renderer.hpp', 'baseline_ui.hpp'),
                       ('src/render_gl/ui_renderer.cpp', 'paired-baseline.cpp')]:
    text = subprocess.check_output(['git', 'show', '1846c37:' + source], text=True)
    text = text.replace('namespace nekomote::render_gl', 'namespace nekomote::baseline_render_gl')
    if source.endswith('.cpp'):
        text = text.replace('#include <nekomote/render_gl/ui_renderer.hpp>', '#include "baseline_ui.hpp"')
        text = text.replace('namespace nekomote::baseline_render_gl\n{',
                            'namespace nekomote::baseline_render_gl\n{\nusing nekomote::render_gl::set_shader_source;')
    (root / target).write_text(text)
```

```bash
c++ -std=c++20 -O3 -flto -Iinclude -Isrc/render_gl \
  -Ibuild/release/gladsources/nekomote_gl_loader/include \
  -I/tmp/nekomote-remaining -c /tmp/nekomote-remaining/paired-baseline.cpp \
  -o /tmp/nekomote-remaining/paired-baseline.o
c++ -std=c++20 -O3 -flto -Iinclude \
  -Ibuild/release/gladsources/nekomote_gl_loader/include -I/tmp/nekomote-remaining \
  docs/validation/remaining-optimization-2026-11-08/whole_frame_probe.cpp \
  /tmp/nekomote-remaining/paired-baseline.o \
  -Wl,--start-group build/release/lib/*.a -Wl,--end-group \
  -lpng -lz -lEGL -lpulse -lfreetype -lfontconfig -lvulkan -ldl -lpthread -lm \
  -o /tmp/nekomote-remaining/whole-frame-ui-probe
flock build/gpu-validation.lock /tmp/nekomote-remaining/whole-frame-ui-probe \
  "$PWD/assets" /tmp/nekomote-remaining/whole-result desktop chocola/wintermaid
```

将 `whole_frame_probe.cpp` 换为 `paired_text_probe.cpp`，去掉最后 model 参数，可只对照 UI。
避免将读回/写 PNG、编译或另一个 GPU/CPU 性能采样放进计时窗口。

最终功能测试使用私有 Xvfb :88、Mesa llvmpipe / lavapipe；没有修改当前桌面。
CTest 的 GPU 项目**内部已经持有** `build/gpu-validation.lock`，不要再在外层重复加同一把锁。
本次第一次外层重复加锁导致等待，停止该批后按下面方式完成重跑，未将卡住的批次计为通过。

```bash
DISPLAY=:88 __GLX_VENDOR_LIBRARY_NAME=mesa LIBGL_ALWAYS_SOFTWARE=1 \
MESA_SHADER_CACHE_DISABLE=true VK_ICD_FILENAMES=/tmp/nekomote-review/lvp.json \
PULSE_SERVER=unix:/tmp/nekomote-remaining/no-pulse \
XDG_CONFIG_HOME=/tmp/nekomote-remaining/config \
  ctest --test-dir build/release --output-on-failure -j1 -L gpu
```
