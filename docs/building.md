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

## 当前检查边界

ABI已与窗口解耦；asan包含CPU ABI合成契约，不构建player/预览/GPU。缺真实素材时相应集成项显式Skipped。C ABI诊断宿主不再依赖Pulse/thread，正式player保留声音服务。

UI测试和Python工具检查已注册CTest：

```sh
ctest --test-dir build/dev                    # 全部测试（含 player_ui、python_tools、python_abi_host）
ctest --test-dir build/dev -LE gpu           # 排除GPU测试（无显示服务器）
ctest --test-dir build/dev -L python         # 仅Python工具检查
ctest --test-dir build/dev -L gpu            # 仅GPU测试（player_ui、renderer_gpu、python_abi_host）
```

| 测试名 | 标签 | 说明 |
| --- | --- | --- |
| `player_ui` | gpu | 独立UI状态测试（8 cases / 124 assertions） |
| `python_tools` | python | 导入/资产保护负例（31 cases） |
| `python_abi_host` | python,gpu | ABI确定性测试（10 cases，需 abi_host + 真实模型） |

缺 `assets/` 时 `python_abi_host` 显式 Skipped（`@unittest.skipUnless`），合成项仍执行。

Fontconfig为UI的可选字体发现入口，未安装时使用源码中的字体路径候选；无可用字体不能宣称UI成功。

## 构建矩阵

G9 验证的构建配置（GCC 15.1.1，全部通过）：

| 配置 | ABI | Preview | Player | Tests | 类型 | 结果 |
| --- | --- | --- | --- | --- | --- | --- |
| `dev` preset | ON | ON | ON | ON | Debug | 138/138 PASS |
| `release` preset | ON | ON | ON | ON | Release+LTO | 通过 |
| `asan` preset | ON | OFF | OFF | ON | Debug+ASan | 136/136 PASS |
| 无ABI纯CPU | OFF | OFF | OFF | ON | Debug | 133/133 PASS |
| 无ABI纯CPU | OFF | OFF | OFF | ON | Release+LTO | 构建通过 |
| preview-only | ON | ON | OFF | ON | Debug | 138/138 PASS |

GCC15 无ABI构建的 `-Wmissing-declarations` 误报已由 G6 的 `$<COMPILE_LANGUAGE:CXX>` 生成器表达式解决，不扩大禁用。

源仓库只读；真实资产和截图放ignored assets/build目录，已跟踪的大型素材已移出索引。本批导入采用新输出目录，已有目录一律EXISTS_UNCHECKED、非零拒绝且保留，不覆盖或认证：

```sh
python3 tools/import_all_assets.py --reference-root ../NekoWebShow \
  --importer build/release/bin/nekomote_import \
  --session-tool tools/import_session/import_session.py \
  --output build/import-new/characters --report build/import-new/report.json
```

当前文件保护已修复，完整账本/异常/CPU结果保留已由G7实现；不将结构导入等同于画面通过。阶段验证范围见 [整合摘要](review-agents-current.md)。
