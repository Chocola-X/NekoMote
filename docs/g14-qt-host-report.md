# G14：Qt诊断宿主自动验收报告

Agent: CodeArts / GLM-5.2
Context: docs/agent-workboard.md G14
基线 HEAD: 9c72813（G15-B 交付）
报告日期: 2026-10-08

## 交付内容

1. `tests/reference_capture/test_qt_quick_host.py`：10 个 Python unittest，覆盖冻结 CLI 错误与生命周期截图。
2. `CMakeLists.txt`：在现有 Python 测试注册区新增 `python_qt_quick_host`，只在 `NEKOMOTE_BUILD_QT_QUICK` 开启时注册，设置 `NEKOMOTE_QT_QUICK_HOST` 环境变量指向 `$<TARGET_FILE:nekomote_qt_quick_host>`，标签 `python;gpu`。

## 测试清单

### CliErrors（6 项，无需真实素材）

| 测试 | 验证内容 |
| --- | --- |
| `test_unsupported_graphics_api` | `--graphics-api vulkan` 返回非零且 stderr 含 `OpenGL` |
| `test_width_zero` | `--width 0` 返回非零 |
| `test_missing_model` | 不存在的模型路径返回非零 |
| `test_existing_output_rejected` | 已存在的输出文件返回非零且 stderr 含 `already exists` |
| `test_help` | `--help` 返回零且 stdout 含 `model` |
| `test_missing_required_option` | 缺 `--model` 返回非零 |

### LifecycleCapture（4 项，需 Qt 二进制 + 真实素材 + Pillow）

| 测试 | 验证内容 |
| --- | --- |
| `test_lifecycle_30_steps` | 30 步 `--check-lifecycle`：initial/resized/recreated PNG 尺寸正确，recreated 与 initial 像素一致，stdout 含 `created=2`/`destroyed=2` |
| `test_capture_chocola` | Chocola `--steps 0 --capture`：PNG 800×1080 |
| `test_capture_fraise_moved_scaled` | Fraise `--origin-x 50 --origin-y 300 --scale 0.8 --steps 0 --capture`：PNG 800×1080 |
| `test_dpi_1_5` | `QT_SCALE_FACTOR=1.5`：PNG 1200×1620 |

## 运行结果

### qt-quick 构建矩阵

```
ctest -R python_qt_quick_host -V
Ran 10 tests in 5.191s
OK
100% tests passed, 0 tests failed out of 1
```

真实运行 10 / Skipped 0 / 失败 0。

### dev 构建（无 Qt 二进制）

```
ctest -R python_tools -V
Ran 168 tests in 5.568s
OK
```

`test_qt_quick_host.py` 中 10 项因 `HOST.exists()` 为 False 整组 Skipped，不计 PASS，不引入 Qt/窗口依赖。

### dev 全量 CTest

```
ctest
100% tests passed, 0 tests failed out of 145
```

## 独立构建矩阵验证

### 矩阵 1：Qt=ON / ABI=OFF（预期失败）

```
cmake -S . -B build/g14-matrix-fail -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DNEKOMOTE_BUILD_QT_QUICK=ON -DNEKOMOTE_BUILD_ABI=OFF
```

结果：CMake Error at CMakeLists.txt:34 — `NEKOMOTE_BUILD_QT_QUICK requires NEKOMOTE_BUILD_ABI`。配置失败，符合预期。

### 矩阵 2：最小 Qt 构建（ABI=ON, preview/player/tools/tests=OFF, Qt=ON）

```
cmake -S . -B build/g14-matrix-qt -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DNEKOMOTE_BUILD_ABI=ON -DNEKOMOTE_BUILD_PREVIEW=OFF -DNEKOMOTE_BUILD_PLAYER=OFF \
  -DNEKOMOTE_BUILD_TOOLS=OFF -DNEKOMOTE_BUILD_TESTS=OFF -DNEKOMOTE_BUILD_QT_QUICK=ON
cmake --build build/g14-matrix-qt --target nekomote_qt_quick_host
```

结果：配置成功，构建成功（38/38）。宿主可执行，`--help` 输出正常。

## 构建模式与二进制

| 项目 | 值 |
| --- | --- |
| 构建模式 | Release |
| 编译器 | GCC 15.1.1 |
| Qt | 6.9.1 |
| 模型 | chocola-date-a, fraise-france-maid-b |
| DPI | 1.0（默认）, 1.5（QT_SCALE_FACTOR） |
| CMakeLists.txt SHA256 | `d8f63c461fbeb96e74948653484a3dd2ca8034e4a3293201201feb440a0a4bc5` |
| qt-quick 宿主 SHA256 | `4f45e0d413ee0ec341649dd55a140a03accc4be05df510b8fc922157977440c1` |
| 基线 HEAD | `9c72813` |

## 未覆盖项

- 逐型号视觉观察交 Gemini V10，不在本任务范围。
- 超高 GLFW 裁切缺口保留未知，不作为 DPI 金标准。
- 长期运行/资源测量不在本任务范围。
- 真实 Plasma 安装验收与普通窗口/API 样例分别报告，不在本任务范围。
