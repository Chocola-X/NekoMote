# G9 交接：检查入口与构建矩阵

Agent: CodeArts / GLM-5.2
基线提交：`5a54bdd`（G8）
完成提交：本文档同批
范围：`apps/player/CMakeLists.txt`、`CMakeLists.txt`、`tests/abi_host/test_abi_host_determinism.py`、`docs/building.md`

## 交付内容

### 1. player_ui 注册 CTest

`apps/player/CMakeLists.txt` 新增：
```cmake
add_test(NAME player_ui COMMAND nekomote_ui_tests)
set_tests_properties(player_ui PROPERTIES LABELS gpu)
```

UI 测试需要 OpenGL（链接 `OpenGL::GL` 和 `nekomote_gl_loader`），标记为 `gpu` label。`-LE gpu` 可排除。

### 2. Python 工具检查注册 CTest

`CMakeLists.txt` 测试部分新增 `find_package(Python3)` 和两个 `add_test`：

| 测试名 | 标签 | 范围 | 说明 |
| --- | --- | --- | --- |
| `python_tools` | python | `tests/reference_capture/` | 31 cases，导入/资产保护负例，不需要 GPU |
| `python_abi_host` | python,gpu | `tests/abi_host/` | 10 cases，ABI 确定性，需要 abi_host + 真实模型 |

`python_abi_host` 通过 `ENVIRONMENT` 传入 `NEKOMOTE_ABI_HOST=$<TARGET_FILE:nekomote_abi_host>`，不硬编码构建目录路径。测试脚本用 `os.environ.get('NEKOMOTE_ABI_HOST', ...)` 读取，缺真实模型时 `@unittest.skipUnless` 显式 Skipped。

### 3. 构建矩阵复核

GCC 15.1.1，全部通过：

| 配置 | ABI | Preview | Player | Tests | 类型 | 结果 |
| --- | --- | --- | --- | --- | --- | --- |
| `dev` preset | ON | ON | ON | ON | Debug | 138/138 PASS |
| `release` preset | ON | ON | ON | ON | Release+LTO | 通过 |
| `asan` preset | ON | OFF | OFF | ON | Debug+ASan | 136/136 PASS |
| 无ABI纯CPU | OFF | OFF | OFF | ON | Debug | 133/133 PASS |
| 无ABI纯CPU | OFF | OFF | OFF | ON | Release+LTO | 构建通过 |
| preview-only | ON | ON | OFF | ON | Debug | 138/138 PASS |

### 4. GCC15 无 ABI 警告

之前记录的 noabi (ABI=OFF) Release + `-Wmissing-declarations` 对标准库内联 `operator delete` 误报，已由 G6 的 `$<COMPILE_LANGUAGE:CXX>` 生成器表达式解决。本轮 noabi Release+LTO 构建通过，不扩大禁用规则。

### 5. 源码副本无 assets/refer 时行为

- `python_tools`（reference_capture 测试）使用合成 fixture，不需要 `assets/` 或 `refer/`
- `python_abi_host` 用 `@unittest.skipUnless(HOST.is_file() and MODEL.is_file(), ...)`，缺 `assets/characters/chocola-date-a/` 时整个测试类 Skipped，不计为 PASS
- C++ 单元测试使用 `tests/fixtures/` 合成 fixture，不需要真实素材
- `refer/` 被 `.gitignore` 忽略，正式构建不依赖它

### 6. docs/building.md 更新

- 新增"构建矩阵"表格
- 更新"当前检查边界"：UI 测试和 Python 工具检查已注册 CTest，不再"独立执行"
- 文档化 `ctest -L python`、`ctest -L gpu`、`ctest -LE gpu` 入口
- 标注 G7 已完成（账本/异常/CPU 结果保留）

## 验证

### dev（完整 player）

```
ctest --test-dir build/dev -j4
100% tests passed, 0 tests failed out of 138
gpu = 2 tests, python = 2 tests
```

测试明细：
- C++ 单元测试：133
- `renderer_gpu`：1
- `player_ui`：1（8 cases / 124 assertions）
- `python_tools`：1（31 cases）
- `python_abi_host`：1（10 cases）
- `abi_test`（integration）：1

### asan（headless ABI/ASan）

```
ctest --test-dir build/asan -j4
100% tests passed, 0 tests failed out of 136
```

### 无 ABI 纯 CPU

```
cmake -G Ninja -S . -B build/noabi -DNEKOMOTE_BUILD_ABI=OFF -DNEKOMOTE_BUILD_PREVIEW=OFF -DNEKOMOTE_BUILD_PLAYER=OFF
ctest --test-dir build/noabi
100% tests passed, 0 tests failed out of 133
```

### preview-only（preview 开/player 关）

```
cmake -G Ninja -S . -B build/preview-only -DNEKOMOTE_BUILD_PREVIEW=ON -DNEKOMOTE_BUILD_PLAYER=OFF
ctest --test-dir build/preview-only
100% tests passed, 0 tests failed out of 138
```

## 未完成 / 交接

- 本轮不改 UI 状态/渲染实现，只注册测试入口和复核构建矩阵
- G5 同条件性能方法延后，不与 G9 同时扩大范围
- UI 稳态分配优化需 profile 后另派
- 完整 195 视觉/交互、8–24 小时 soak、多屏生命周期与安装包仍是最终交付条件
