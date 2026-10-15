# Wayland 协议来源

仅在构建 layer-shell 适配器时由 `wayland-scanner` 生成客户端代码。
XML 保留各自内嵌许可；不引入参考仓库或桌面实现源码。

| 文件 | 固定来源 | SHA256 |
| --- | --- | --- |
| `wlr-layer-shell-unstable-v1.xml` | [upstream](https://raw.githubusercontent.com/swaywm/wlr-protocols/b010a03648b88d143236de193bddbfea0c08bc84/unstable/wlr-layer-shell-unstable-v1.xml) | `996b8c66e1c2276c443336d4cb174634e8e5948823779042f244930525b79cc2` |
| `xdg-shell.xml` | [upstream](https://raw.githubusercontent.com/glfw/glfw/3.4/deps/wayland/xdg-shell.xml) | `b72112a7a1f9b2ad94ffa59468753ced568d6e7b2069df94a9d437e488b8506e` |

本地修改：`wlr-layer-shell` 的 `get_layer_surface` 参数名 `namespace` 改为 `namespace_`，避免生成 C++ 头文件时使用保留字；不改变 wire 接口、参数顺序或类型。上表记录上游原文件摘要，修改后本地 SHA256 为 `b9bfe7411d3fe67e3bb59e3a9e1b04b948fd1e16f15941aab160691722e225cc`。
