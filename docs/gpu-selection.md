# 独立进程选择 GPU

player、X11、layer-shell 支持仅对本次进程生效的 `--gpu`，使用 Linux PCI 地址：

```bash
nekomote_player --list-gpus
nekomote_x11 --assets /path/to/assets --gpu pci:0000:06:00.0
nekomote_layer_shell --config ~/.config/nekomote/right.json --backend vulkan --gpu pci:0000:01:00.0
```

`--list-gpus` 无需资产、启动配置或显示服务器，列出 PCI 显示类硬件的型号、驱动和 vendor/device
ID。型号来自发行版的 `pci.ids` 数据库；数据库缺失或未收录时标为未知，仍保留 PCI 地址和数字 ID。硬件列表不表示当前 OpenGL/Vulkan 驱动已识别它，也不表示已验证目标输出的呈现能力。
地址取自本机枚举，不使用示例地址猜测自己的设备；PCI 拓扑变化后需重新确认。

不传参数时保持既有驱动环境和设备选择策略。Vulkan 默认仍按设备类型评分、优先独显。
显式选择后，程序验证实际设备身份并在初始化日志中打印名称和 PCI 地址；无法确认身份、
缺少必要驱动/扩展、目标不支持呈现或设备不存在时失败退出，不自动改选其他 GPU。

GPU 参数不写入启动文件或面板偏好；`--gpu` 与 `--save-config` 组合会拒绝保存。长期
固定选择可将参数加入自启动命令或 systemd 用户服务。更换设备需要重启进程。
Plasma 插件继续跟随宿主，不支持每个壁纸实例独立选卡。

OpenGL 使用 Mesa 的 PCI `DRI_PRIME` 或 NVIDIA PRIME Render Offload，在窗口库初始化
前设置必要的本进程环境。EGL 通过设备 DRM 节点核验 PCI；NVIDIA 可通过 GL 设备 UUID
关联驱动信息。GLX Mesa 用 renderer vendor/device 信息关联本机唯一 PCI 设备；存在
同型号多卡、无法唯一关联时拒绝验证。多个 NVIDIA 专有驱动 GPU 的 OpenGL 显式选择
需要额外的 provider 映射，当前明确拒绝。软件渲染器没有可伪造的 PCI 身份。

Vulkan 保持 1.0 接入，通过可选 KHR properties2 和 EXT pci_bus_info 获取 PCI 信息，
显式匹配后仍检查目标 surface 呈现队列。仅修改枚举顺序的外部环境变量不能代替明确匹配。
指定核显不保证省电，跨 GPU 呈现可能增加传输成本；资源收益需在目标系统独立测量。
