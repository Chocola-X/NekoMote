# Plasma 设置页与显示名称

从 `6e5144e` 更新 Plasma 5 / 6 壁纸设置页：补充资产根目录与后端切换说明，添加
GitHub、个人网站链接，说明文字和链接使用控件主题颜色。两版均以英文元数据名称和
`zh_CN` 名称分别显示 NekoMote Wallpaper / NekoMote 壁纸与
NekoMote Desktop / NekoMote 桌面。

Plasma 6 的已有简洁 containment 改显示名称，ID `org.nekomote.validation` 保持不变。
Plasma 5 补齐同 ID 的最小 Qt Quick containment 和安装入口；两者均不提供文件夹视图
功能，不改变壁纸 ID、配置字段、C ABI 或应用/渲染逻辑。设置页说明仍为中文。

验证使用本机 Qt 5.15.17 / Qt 6.9.2，未代表 Ubuntu20 / Debian12 基线或 ARM64 验收：

- `build/plasma5` 和 `build/plasma` 的 Release/LTO stage 构建通过，使用项目内缓存依赖。
- Qt 5 `plasma_module_import`、`plasma5_package_import` 两项通过，后者包含新增布局的
  QML 导入。Qt 6 用既有 `test_qml_module.cpp` 验证安装包模块导入通过。
- 两版安装包设置页实际加载，验证目录、后端绑定与两个链接文本；Qt 5 使用可见 X11
  测试窗口，Qt 6 使用 offscreen/software。截图已目视检查。
- 16 项既有分发/部署 Python 回归通过；两包各 10 个 manifest 文件哈希、中英文元数据
  名称及链接检查通过；修改的 C++ 通过 clang-format，`git diff --check` 通过。

本地截图与临时诊断入口在 `build/evidence/plasma-settings-20261021/`。没有安装或切换
真实桌面，没有执行远端 Actions；新增 Qt 5 布局在真实 Plasma 宿主中的列出/激活尚未
实测。没有更改帧路径或新增依赖，不声称性能改善。

执行：Codex，GPT-6（精确变体未提供）。
