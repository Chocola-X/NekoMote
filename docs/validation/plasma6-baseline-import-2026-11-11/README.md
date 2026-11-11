# Plasma6 Qt6.4 基线包内导入补修

2026-11-11，从 `8ca7c89` 修复。用户提供双架构 CI evidence ZIP；日志摘要保存在本目录。
源文件、wrapper、原 Actions 后端库和最终测试包 SHA256 见 environment.json。

## 根因与修复

- 两架构编译和 258 个必选 CTest runner 通过；最终包额外导入检查失败。
- Qt6.4.2 要求 qmldir 中声明的 module 与 import URI 一致。目录导入使用基于路径的 URI，
  原包仍声明 `NekoMote.Wallpaper`。此前 Qt6.9 成功不能覆盖最低 Qt 版本。
- 去掉声明后仅能加载插件，原生类型仍在固定 named URI 下注册，所以 native item 不可见。
- 导出副本统一去 module/prefer；Qt6 插件改用 QQmlExtensionPlugin，在目录 URI 注册同一
  NekoWallpaperItem，named URI 保留生成注册器，避免重复注册。翻译仍由 initializeEngine
  逐 engine 安装，原 stage 不改。没有提高 Qt 基线、删除检查或修改求值/绘制。

Qt6.4 官方实现的 registerPluginTypes 会先调用插件注册，再调用 generated module registrar：
[qqmlmetatype.cpp](https://github.com/qt/qtdeclarative/blob/v6.4.2/src/qml/qml/qqmlmetatype.cpp)。

## 同环境前后验证

从 Debian 官方 bookworm 包解出 Qt6.4.2 runtime、开发头和 moc，全部保存在项目内。
包名、版本和下载 SHA256 见 qt64-packages.txt；没有安装或替换主机系统包。
测试使用已有 Actions 中的 Qt6.4.2 原生后端库，probe 和 wrapper 通过主机编译器按旧头重新编译。
Qt 路径通过 probe 旁的 qt.conf 指向隔离标准库目录，两个 QML_IMPORT 环境变量均为空。

| 验证 | 结果 |
| --- | --- |
| 原包 / Qt6.4.2 | 拒绝，复现 CI 相同 namespace/URI 错误 |
| 只删除 module/prefer / 原插件 | 拒绝，NekoWallpaperItem 不可见 |
| 完整修复 / Debug、Werror wrapper | 设置页优先及 native item 导入通过 |
| 最终 bundle / Qt6.4.2 | ELF/Qt 基线审计、内层版本/平台/哈希检查通过 |
| 最终 bundle / Qt6.4.2 | 目录与 named 两个入口通过 |
| 最终 bundle / Qt6.4.2 | en_US、zh_CN、zh_TW 设置页翻译与第二 engine 检查通过 |
| 删除最终包插件 / Qt6.4.2 | 按预期拒绝，排除全局模块借用 |
| 本机 Qt6.9.2 / Release、LTO、Werror | wrapper、stage 和相关测试目标重新构建通过；4 个既有 Qt runner 通过 |
| Qt6.9.2 与 Qt5.15.17 / 最终包 | 两版本设置页优先和 native 目录导入通过 |
| Python 工具全集 | 180 项，12 项按环境跳过 |

源码导出回归测试同时检查 Qt5/Qt6 均无 module/prefer，源 stage 保留 named 声明和二进制字节。
独立 Agent 复核 URI 注册、generated registrar、LTO 保留和多 engine 生命周期，未发现阻塞问题。

## 边界与复现入口

这不是完整 Debian12 / GCC12 构建：glibc、C++ 标准库及部分图形依赖来自主机；
baseline wrapper 使用主机编译器，Debug 开启断言。没有重跑 aarch64 或远端 Actions，
没有实际 GPU 渲染/桌面安装/商店下载认证，也没有采样帧性能。渲染热路径未改。
Qt6.4 的“缺少 module identifier、不能保护外部注册”提示是目录模块的预期警告，
不代表导入失败；保留日志，没有全局屏蔽。完整隔离文件、脚本与归档保留在被忽略的
experiments/plasma6-baseline-fix/。

```bash
python3 -m unittest discover -s tests/reference_capture
cmake --build build/plasma --target nekomote_plasma_module_test nekomote_plasma_translation_test nekomote_plasma_stage
ctest --test-dir build/plasma -R 'plasma_module|plasma_translation' --output-on-failure
```

Qt6.4 定向复现使用 tests/plasma/kpackage_import/main.cpp、tests/plasma/test_qml_module.cpp
及 tests/plasma/test_translations.cpp；远端两架构仍由原 CI 最终包导入门禁验收。
