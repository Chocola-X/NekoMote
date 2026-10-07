# G8 交接：确定性 C ABI 诊断工具

> Codex复核与后续修补以 [当前整合摘要](review-agents-current.md) 为准；下文保留本任务交付时的验证范围。

Agent: CodeArts / GLM-5.2
基线提交：`679a8ce`（G7）
完成提交：本文档同批
范围：`apps/abi_host/main.c`、`apps/abi_host/CMakeLists.txt`、`tests/abi_host/test_abi_host_determinism.py`（新）

## 交付内容

### 1. 两种运行模式明确分离

- **固定步模式**（`--steps N`，N>0）：每步 `dt=1/60` 秒，不依赖墙钟，N 步后自动退出。`glfwSwapInterval(0)` 禁用 vsync。截图在最后一帧 `render` 后、`glfwSwapBuffers` 前 `glReadPixels` 读回，保证确定像素。
- **交互模式**（`--steps 0`，默认）：用墙钟 `dt`（上限 0.25s），`glfwSwapInterval(1)` 启用 vsync，运行直到 Esc 或窗口关闭。截图在循环退出后读回，像素可能受 swap 后 framebuffer 不确定影响，仅作粗略诊断。

### 2. 严格数值解析

新增 `parse_long`、`parse_uint32`、`parse_window_size` 函数，用 `strtol`/`strtoul` + `endptr` 检查：
- 尾随字符（`endptr` 不是字符串末尾）→ 拒绝
- 负数（uint32 参数首字符为 `-`）→ 拒绝
- 溢出（`errno == ERANGE` 或 `> UINT32_MAX`）→ 拒绝
- 空字符串 → 拒绝
- 窗口尺寸范围 1..65535 → 超界拒绝

所有数值参数（`--width`、`--height`、`--steps`、`--seed`）均用严格解析，拒绝时退出码 2。

### 3. 路径截断检查

- `MAX_PATH` 从 1024 增至 4096
- `dir_of` 检查截断并返回失败（原静默截断）
- `join_path` 已有截断检查

### 4. 纹理加载失败立即非零

`upload_textures` 原在纹理加载/上传失败时 `continue`（跳过）。改为失败返回 -1，main 检查后 `goto fail_renderer` 退出码 1。用缺纹理的真实合成资产验证（复制 chocola-date-a 后删除 textures/ 目录）。

### 5. PNG 编码（替换 raw RGBA）

新增最小 PNG 编码器，用 zlib `compress` + `crc32`：
- 8 字节签名
- IHDR chunk（width, height, bit_depth=8, color_type=6 RGBA）
- IDAT chunk（zlib 压缩的扫描线数据，每行前置 filter byte 0）
- IEND chunk

截图输出可解码 PNG，文件扩展名 `.png`。垂直翻转在编码前完成。

### 6. 截图在 swap 前读回

固定步模式最后一帧 `render` 后、`glfwSwapBuffers` 前 `glReadPixels`，避免交换缓冲后读取不确定内容。交互模式在循环退出后读回（像素稳定性不保证，仅作粗略诊断）。

### 7. 确定性验证

两次同输入（同 model、session、steps=20、seed=42、width=160、height=120）产生：
- 相同的 PNG 文件 hash
- 完全相同的像素 hash
- 相同的压缩大小

### 8. 禁止 OS 键鼠注入

只用 GLFW 事件（`glfwPollEvents`），不注入系统键鼠、不按标题选择窗口。

## 验证

### C 单元测试

```
ctest --test-dir build/dev -j4
136/136 tests passed
```

### Python 确定性测试

```
python3 -m unittest tests.abi_host.test_abi_host_determinism -v
Ran 10 tests in 2.151s
OK
```

覆盖：
- 尾随字符、负数、溢出、超界、空字符串拒绝（5 项）
- 固定步 PNG 可解码（1 项）
- 两次同输入像素相同（1 项）
- 缺纹理退出非零（1 项）
- 不同 seed 不崩溃（1 项）
- 超界 width 拒绝（1 项）

### 全部 Python 测试

```
python3 -m unittest discover -s tests/reference_capture
31/31 OK
python3 -m unittest discover -s tests/abi_host
10/10 OK
```

### 真实截图确定性

```
./build/release/bin/nekomote_abi_host \
  --model assets/characters/chocola-date-a/model.nkm.json \
  --session assets/characters/chocola-date-a/session.nks.json \
  --steps 30 --seed 42 --capture /tmp/g8-cap.png --width 320 --height 240
```

两次运行 PNG 文件 hash 相同：`6e88d2c17cd8c810`，像素 hash 相同：`e9315d2f970d500c`。

## 未完成 / 交接

- 本工具仍是静音 C ABI 诊断程序，不是 Plasma 壁纸插件
- 交互模式的截图像素稳定性不保证（墙钟 dt + swap 后读回）
- 与 C++ 诊断输出的逐像素对照交 Codex C7（需要 C++ 端固定步截图入口）
- 本轮不改公共 ABI，只调用既有 ABI 及容量 getter
- zlib 是系统标准库，许可兼容，用于诊断工具的 PNG 编码
