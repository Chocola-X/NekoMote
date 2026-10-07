# 构建与验证

## 前置

- CMake ≥ 3.25
- Ninja
- C++20 编译器（GCC ≥ 13 或 Clang ≥ 16；开发机验证 GCC 15.1.1 / Clang 20.1.8）
- 首次配置需联网（FetchContent 拉取依赖）
- 图形 loader 生成需要 Python 3 与 Jinja2。
- 图形宿主额外需要：OpenGL 3.3 core 驱动 + X11/Wayland 显示服务器
- 普通播放器额外需要：pkg-config 和 libpulse 17.x 开发包（`NEKOMOTE_BUILD_PLAYER=OFF` 可关闭）

## Presets

| Preset | 类型 | 特性 | 预览 | 测试 |
| --- | --- | --- | --- | --- |
| `dev` | Debug | 警告即错误、compile_commands.json | ✓ | ✓ |
| `release` | Release | LTO、警告即错误 | ✓ | ✓ |
| `asan` | Debug | ASan + UBSan、警告即错误 | ✗ | ✓ |

`asan` 不构建预览：GLFW + 图形驱动与 ASan 混用会产生大量非项目噪声，核心与测试路径已覆盖语义。

## 命令

```bash
# 配置
cmake --preset dev

# 构建
cmake --build --preset dev

# 测试
ctest --preset dev

# 运行预览（需显示服务器）
./build/dev/bin/nekomote_preview

# 格式检查（按 .clang-format）
clang-format --dry-run --Werror $(git ls-files '*.h' '*.hpp' '*.c' '*.cpp')
```

## 产物布局

```
build/<preset>/
  bin/nekomote_player       # 普通交互播放器
  bin/nekomote_preview      # 诊断预览
  bin/nekomote_unit_tests   # 单元测试可执行
  lib/libnekomote_model.a   # 核心静态库
  compile_commands.json     # IDE/clangd 索引
```

## 无显示服务器环境

核心库与单元测试不需要显示服务器：

```bash
cmake --preset dev -DNEKOMOTE_BUILD_PREVIEW=OFF -DNEKOMOTE_BUILD_PLAYER=OFF
cmake --build --preset dev
ctest --preset dev
```

## 缺少 refer/

`refer/` 被 `.gitignore` 忽略，是本地调查材料。正式构建不依赖它：缺失 `refer/` 不影响配置和构建，只影响显式素材调查工具（P1 起）。

## 当前资产与 P2 部件诊断

```bash
build/dev/bin/nekomote_import ../NekoWebShow/model_conversion/chocola/chocola-date-a/chocola-date-a.json \
  ../NekoWebShow/model_conversion/chocola/chocola-date-a/chocola-date-a.resx.json --output assets/characters/chocola-date-a
build/dev/bin/nekomote_preview --asset assets/characters/chocola-date-a/model.nkm.json \
  --icon tex/0003 --origin 400 651 --hidden --screenshot build/mouth.png
ctest --preset dev -L gpu  # 需要显示服务器，执行真实 GPU 像素/生命周期回归
```

上述入口为部件诊断。当前整模型普通窗口入口为 `nekomote_player`；导入会话、真实声音、完整适配和交互见 [原生播放器](native-player.md)。无显示环境运行 `ctest --preset dev -LE gpu`；`asan` 不构建 GPU 测试。参考截图命令见 [reference_capture/README.md](../tools/reference_capture/README.md)。

## 新宿主与清洁构建注意（2026-10-07审查）

当前dev128项、无窗口ASan126项；ASan不编译ABI/C宿主/UI。ABI集成测试目前依赖assets中的真实Chocola文件，缺素材的clean checkout还不能把此测试计为通过。拆分合成契约测试/显式真实素材集成入口见G1/G6。

播放器UI使用Fontconfig；目前CMake可选探测、源码无条件使用，需统一为明确依赖。C ABI宿主仍直接链接pulse-simple/pulse/pthread，关闭PLAYER不能保证不需要这些依赖；这是待修复项，不把当前构建组织写作目标设计。

源仓库只读。真实素材在assets中导入，截图/trace/基准在build/evidence中；已跟踪的大型资产需移出索引保留本地文件，不能只新增ignore规则。批量导入工具当前有绝对路径/partial输出缺口，正式使用前执行G4。
