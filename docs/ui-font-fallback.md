# 共享 UI 字体发现与回落（2026-10-18）

F1，base `842f7cc`。用户反馈朋友的 Arch Linux / Hyprland 上，自编译与发行包的
player / layer-shell 均出现 UI 字体初始化失败；随后要求优先选择覆盖英文、简中、
繁中、日文的常见字体，失败或缺字后再回落，并控制内存。
执行者：Codex；模型精确变体未由当前会话提供，不猜写提交溯源。

原实现构建时可选 Fontconfig；缺失时只检查四个固定的 CJK 字体路径。启用时只加载
首个匹配文件，失败不重试，没有缺字回落。朋友机器的实际匹配输出尚未取得，不能
确认其具体失败分支。首轮修复曾预加载所有能扩展字符覆盖的字体，实测打开 206 个
字体文件，策略过宽，已被本轮按实际文本准备的实现替代。

## 当前行为

- Fontconfig 为 UI 必需依赖，删除固定路径分支；字体由系统/用户配置定位。
- 首选完整 CJK 家族：`Noto Sans CJK SC`、`Noto Sans CJK TC`、`Noto Sans CJK JP`，
  然后 `Source Han Sans SC/TC/JP`、`Source Han Sans`；其后才是 `Noto Sans`、
  `DejaVu Sans`、`Liberation Sans`、`Ubuntu`、`Cantarell`、系统 `sans-serif`。
  主匹配带 Latin、简繁汉字和假名代表字符的覆盖提示，不以族名证明全部字形可用。
- 初始化通过 `FcFontMatch` 只加载一个主字体，加载失败才调用 `FcFontSort` 重试。
- `FontMetrics::prepare_text` 是显式内容边界：检查实际所需字符，只为仍缺失的字符
  查询和加载候选，已加载的相同文件/face 不重复打开；准备字形并调用后端上传回调。
  显式文件路径模式只使用指定文件。所有安装字体均缺字时保留主字体缺字标记。
- 共享 `prepare_menu_text` 一次收集全部四种 UI 语言和目录的本地化名称。player 在
  初始化调用；ABI 在首个有效绘制上下文、目录变更或资源重建后调用。切换语言无需
  扫描字体，稳定帧的度量/绘制不发现或加载字体。GPU 上传留在后端回调。
- 度量和位图使用同一字体，共同主字体基线；字形缓存保留来源和 advance。
- renderer / 字体 / atlas 失败分别保留诊断，字体错误包含文件与原因。

这些家族是优先候选，不保证所有发行版预装 CJK 字体。
Noto 官方区分完整字符覆盖版本与地域子集版本，见
[Noto CJK 格式说明](https://github.com/notofonts/noto-cjk/tree/main/Sans)。
覆盖四语字形与语言特定字形替换 `locl` 属于不同范围，本轮不实现 shaping。
Qt 的公开约定同样包括主字体匹配、具体缺字回落和匹配缓存，见
[QFont](https://doc.qt.io/qt-6/qfont.html)；Fontconfig 提供
[匹配与排序接口](https://fontconfig.pages.freedesktop.org/fontconfig/fontconfig-devel/)。
本轮复用已有字体栅格器，没有引入 Qt/Pango 依赖。

## 验证

- Debug / Werror 的 CPU probe、unit、player、ABI 和 GL/Vulkan GPU 测试目标构建成功。
- Release / LTO / Werror 的 GL/Vulkan player、ABI 和独立 layer-shell 构建成功。
- `ctest --test-dir build/dev -LE 'gpu|integration' --output-on-failure -j4`：199/199。
- `ctest --test-dir build/dev -R '^ui_font_|font atlas growth' --output-on-failure`：6/6。
  五项新 probe 检查空字体目录诊断、任意路径发现、损坏首选/全部候选失败来源、
  按需缺字回落、CJK 主字体四语覆盖；既有 atlas 扩容检查通过。
- 缺字检查比较备用字体与混合字体的 bitmap hash、尺寸和宽度；原 Latin 字形一致。
  字体映射计数验证 ASCII 初始化/准备/重复准备为 `1/1/1`，需要 Thai 时为 `1/2/2`。
  CJK 与 Latin 字体同时存在时，四语样例准备/重复准备为 `1/1/1`；英文、简中、繁中、
  平假名/片假名样例均与缺字标记不同。
- CJK 检查使用临时下载的官方 `NotoSansCJKsc-Regular.otf`，16437364 字节，SHA256
  `2c76254f6fc379fddfce0a7e84fb5385bb135d3e399294f6eeb6680d0365b74b`，未安装系统字体。
  可用 `NEKOMOTE_TEST_CJK_FONT` 显式指定；缺失适用字体/工具时 integration 明确跳过。
- 隐藏窗口 GL UI 重建与 ABI 延迟输出：2 用例 / 43 assertions；Vulkan UI 绘制顺序、
  资源重建与预热分配：2 用例 / 48 assertions。首次沙箱 GLFW 连接失败；授权显示
  连接后通过，持有 GPU 锁。没有输入注入、部署桌面或改写会话配置。
- 格式、`git diff --check`、CI 脚本语法通过；CI 必选构建目标包含字体 probe。

## 内存与帧路径

本机 Arch / Xeon E5-1680 v4，CPU 独立探针，无显示/GPU/模型/呈现 FPS。
用 `/proc/<pid>/smaps` 区分字体映射、Fontconfig、匿名/heap 与其余页面。
`mmap` 的虚拟文件长度不是实际 RSS；读取字体头、字符表及栅格数据会使文件页驻留。
首轮约 10 MiB 的差值主要是数百个字体的文件页，不是额外一份同样大的字符映射表。

相同 53 字符的中日英标签、相同 `-O3 -DNDEBUG` 探针，当前在内容准备阶段解析/预热：

| CPU 探针阶段 | 首轮预加载 | 当前按需 |
| --- | ---: | ---: |
| 初始化后映射字体文件 | 206 | 1 |
| 初始化后字体页 RSS | 12804 KiB | 128 KiB |
| 标签绘制后字体页 RSS | 14104 KiB | 704 KiB |
| 标签绘制后进程总 RSS | 22312 KiB | 8876 KiB |

其中字体页大部分为只读文件页，可共享/回收，不等同私有堆。当前额外检查完整四语菜单
的静态文本（空目录）准备后仍只映射一个字体，字体页 RSS 2492 KiB，总 RSS 11048 KiB。
本机实际主字体由已有用户配置解析；官方 Noto CJK 使用独立临时配置验证，未安装。
字体集合、文字和系统页缓存状态影响这些数字，不推定为整个壁纸应用的固定收益。

相同标签预热 1000 次，每轮 20000 次 `draw_text + text_width`，复用输出容量，3轮：
首轮/当前 P50 为 2.622–2.632 / 2.602–2.614 µs，P95 为 2.629–3.723 /
2.628–3.632 µs，P99 为 2.631–7.867 / 2.631–3.884 µs，常规 `new` 计数均为 0。
初始化 23.5–26.8 / 19.3–23.6 ms（当前显式文本准备时间另计）。主字体变化导致宽度
变化，不宣称整体性能提升；尾部波动保留。GPU 测试覆盖预热分配，不是发布态 GPU
帧时间、显存或功耗测量，也不替代朋友的 Arch/Hyprland 或长期验收。

本地源码、逐映射数据和输出位于 `build/evidence/font-fallback-eager/` 与
`build/evidence/font-fallback-demand/`；不作为正式构建/分发依赖。当前仍需朋友重测。
