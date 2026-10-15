# layer-shell 隔离验收

此入口只显式连接自己创建的 Wayland socket，不连接当前用户桌面，不注入系统输入。
它使用已有 `kwin_wayland --virtual`，无需安装其他桌面或切换登录会话。

```bash
python3 tools/testing/run_layer_shell_test.py \
    --binary build/layer-shell-validation/bin/nekomote_layer_shell \
    --importer build/layer-shell-validation/bin/nekomote_import \
    --compositor /usr/bin/kwin_wayland \
    --no-protocol-server build/layer-shell-validation/bin/nekomote_layer_shell_no_protocol_server \
    --evidence-dir build/evidence/layer-shell-new-run
```

证据目录必须不存在。脚本创建私有 HOME、XDG runtime/config/data/cache/state 和
DBus 会话，持有项目 GPU 验证锁，按进程限制超时并清理自己启动的进程。
运行仅需 Python 标准库、KWin、DBus 和显式传入的三个项目二进制。
可加 `--real-assets /path/to/assets`，在 1280×720 虚拟输出补充真实资产加载检查；
它使用独立配置和诊断 no-save，不写入素材目录。

脚本使用项目自己的合成模型生成工具与导入器，不依赖真实角色素材。
验收包含：无显示连接、无 layer-shell 协议、双输出枚举及指定、错误输出拒绝、
interactive/passthrough 两种模式的渲染、不同初始输出尺寸与缩放、固定步画面重复一致、
资产目录与显式模型入口均显示合成材质、已有截图保护、合成器断开与 SIGTERM 清理，
以及诊断模式不保存配置。scale2 除 KWin 参数外，还设置私有
`config/kwinoutputconfig.json`，并断言客户端实际报告 scale2，避免把分辨率翻倍误记为
buffer scale 通过。输出日志、合成输入、PPM 和 results.json 保存在证据目录。

本机 NVIDIA/KWin 虚拟 OpenGL 后台存在图像导入错误，客户 framebuffer 读回不能替代
合成器最终画面。可显式选择已安装的 Mesa 软件栈运行隔离协议测试：

```bash
KWIN_COMPOSE=Q LIBGL_ALWAYS_SOFTWARE=1 \
__EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/50_mesa.json \
python3 tools/testing/run_layer_shell_test.py <上述参数，使用新的证据目录>
```

此环境仅作用于新进程；不是性能基准，也不代表硬件驱动和物理输出通过。

`no_layer_shell_server.c` 是负例服务器，提供可绑定的 wl_compositor global，故意不提供
layer-shell；宿主应在创建 surface 前因能力不足而拒绝。它不能作为渲染服务器使用。
其 socket 只允许在当前用户拥有的 mode-0700 私有 runtime 中创建。

KWin 虚拟输出通过不代表 Sway、Hyprland 或 niri 已通过；运行中热插拔/resize、真实输入、
真实模型动态、声音、真实显示器、资源消耗和长期运行另行验收。不同初始尺寸/缩放的进程
重启不能替代运行中输出变化测试。
