# 构建与验证

## 前置

- CMake ≥ 3.25
- Ninja
- C++20 编译器（GCC ≥ 13 或 Clang ≥ 16；开发机验证 GCC 15.1.1 / Clang 20.1.8）
- 首次配置需联网（FetchContent 拉取依赖）
- 图形 loader 生成需要 Python 3 与 Jinja2。
- 预览额外需要：OpenGL 3.3 core 驱动 + X11/Wayland 显示服务器

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
clang-format --dry-run --Werror $(git ls-files '*.hpp' '*.cpp')
```

## 产物布局

```
build/<preset>/
  bin/nekomote_preview      # 预览可执行
  bin/nekomote_unit_tests   # 单元测试可执行
  lib/libnekomote_model.a   # 核心静态库
  compile_commands.json     # IDE/clangd 索引
```

## 无显示服务器环境

核心库与单元测试不需要显示服务器：

```bash
cmake --preset dev -DNEKOMOTE_BUILD_PREVIEW=OFF
cmake --build --preset dev
ctest --preset dev
```

## 缺少 refer/

`refer/` 被 `.gitignore` 忽略，是本地调查材料。正式构建不依赖它：缺失 `refer/` 不影响配置和构建，只影响显式素材调查工具（P1 起）。

## 当前资产与 P2 部件诊断

```bash
build/dev/bin/nekomote_import ../NekoWebShow/model_conversion/chocola/chocola-date-a/chocola-date-a.json \
  ../NekoWebShow/model_conversion/chocola/chocola-date-a/chocola-date-a.resx.json --output build/assets/chocola-date-a
build/dev/bin/nekomote_preview --asset build/assets/chocola-date-a/model.nkm.json \
  --icon tex/0003 --origin 400 651 --hidden --screenshot build/mouth.png
ctest --preset dev -L gpu  # 需要显示服务器，执行真实 GPU 像素/生命周期回归
```

P2 只具备图集部件诊断，未实现整模型求值；不传 --icon 时明确拒绝整模型播放。无显示环境运行 `ctest --preset dev -LE gpu`；`asan` 不构建 GPU 测试。参考截图命令见 [reference_capture/README.md](../tools/reference_capture/README.md)。
