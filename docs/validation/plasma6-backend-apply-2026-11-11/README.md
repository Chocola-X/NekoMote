# Plasma6 应用后端配置后 Loading 的验证

2026-11-11，基线 de456c5。用户在 Manjaro24 / KDE6 Live（内核6.9）报告：
资产应用后 OpenGL 正常，改选 Vulkan 后只有红色 Loading；换桌面布局后恢复。
未取得该 Live 的 Qt/Plasma 精确版本或 QML 日志，不把内核版本当作 Qt 版本。

## 确认的缺口

原 Plasma6 入口仅在 Component.onCompleted 生成 UUID。配置映射替换/重置而对象
存活时，丢失 ID 会让 native assetsDir 门控变为空；后端即使已是 vulkan，status 仍为0。
测试使用真实入口 QML 和 stage 原生模块，仅替换 WallpaperItem 的宿主配置属性：
旧代码在同一对象换入有资产/Vulkan、无 UUID 的 map 后稳定失败，打印
`id= assets= backend=vulkan status=0`，未重新创建对象。

修复在配置 ID 变化后延迟检查当前 map，不捕获旧 map、不使用轮询 timer。
同一壁纸对象记忆最近的非空 UUID，空 ID 恢复该值；首次无 ID 才生成。
显式已有 ID 优先并更新记忆，避免应用设置后转向另一份面板偏好。

## 验证

- 同一回归源码：原 main.qml 返回3，修复后通过。
- 覆盖首次分配、GL→VK map 替换、VK→GL、原 map ID 清空、暂态空 map 后恢复有效 map、
  第二个显式 UUID→清空→恢复该 UUID，以及销毁/重建后沿用已有 UUID、不重复保存。
- 本机 Release/LTO/Werror 构建通过；5 项 Qt CTest runner 通过（新增配置生命周期1项，
  原模块导入1项和翻译3项）。新 target 已加入 Plasma6 CI 显式构建列表。
- 从真实外层包解出的 main.qml 和包内目录模块运行同一回归通过；清空全局 QML 路径。
- 同一测试源码按隔离 Qt6.4.2 headers/moc 编译、配已有 Qt6.4 native stage，修复通过。
  该定向检查仍使用主机编译器/libc，不是完整 Debian12 容器或 ARM64 验收。
- Python 工具全集180项、12项按环境跳过；格式与 shell 语法检查通过。
- 独立执行者复核当前 map、延迟执行、已有/新显式身份和组件初始化时序，无阻塞问题。

这是配置链回归，使用 offscreen/software，status1只表示配置门控解除，不代表 Vulkan
GPU 已绘制。未修改用户桌面、未安装新包、未直接复现目标 Live 的完整“应用配置”操作，
也未证明该 Live 一定重置了 map/UUID。该机器仍需用新包复测；若持续 Loading，应记录
UUID、native assetsDir、status及 QML 警告，再判断是否有独立的 renderer 重建故障。

复现：

```bash
cmake --build build/plasma --target nekomote_plasma6_configuration_test
ctest --test-dir build/plasma -R 'plasma6_configuration|plasma_module|plasma_translation' --output-on-failure
```

测试宿主 fixture 不作为真正 Plasma/窗口/GPU 测试。没有修改播放热路径，未做性能采样；
新增逻辑只在配置变化和初始化时执行。实验文件保存在 experiments/plasma6-config/。
