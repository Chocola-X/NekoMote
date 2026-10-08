# G15 Plasma/QML 原型报告

更新：2026-10-08。执行 CodeArts / GLM-5.2。基线提交 `214fd24`（G15-A）。
独立实现共享 Qt 桥、QML 模块与隔离 Plasma 包原型。不改求值/GPU 公式、ABI 签名或外部素材。

## G15-A：共享 Qt 桥提取

将 `apps/qt_quick_host/session_item.*` 移为 `src/host_qt_quick/` 私有 Qt 共享桥：
- 静态库 `nekomote_host_qt_quick`，PIC，AUTOMOC，公开 include
- 提取 `SessionItemBase` 抽象基类（Q_OBJECT，frame_ready/failed 信号）
- 提取 `InputKind`/`Input`/`ItemState` 到命名空间级别
- 工厂函数 `create_session_renderer()` 供两个调用者复用
- `SessionItem` 继承 `SessionItemBase`，实现 `get_item_state()`/`clear_inputs()`
- 诊断宿主链接共享桥，行为不变

验证：Chocola/Fraise `--steps 0` 截图与 C7 冻结图 SHA 完全一致。
提交 `214fd24`。

## G15-B：QML 模块与 Plasma 包

### NekoWallpaperItem QML 原生组件

`adapters/plasma/wallpaper_item.hpp/cpp`：
- 继承 `SessionItemBase`，QML_ELEMENT/QML_NAMED_ELEMENT
- Q_PROPERTY：modelFile/sessionFile（QUrl）、originX/Y、scale、paused、status、errorString
- status：0=Unconfigured, 1=Loaded, 2=Failed, 3=UnsupportedBackend
- 组件创建时加载模型/会话；后续改源明确拒绝并报告 errorString
- 输入/暂停交给共享桥，不复制反应/物理/纹理管线/声音/背景
- 渲染复用 `create_session_renderer()`，无第二份渲染器实现

### 隔离 QML 验证宿主

`adapters/plasma/probe/main.cpp`：
- 命令行：--model/--session/--capture/--steps/--origin-x/--origin-y/--scale/--dpi/--graphics-api
- 加载内联 QML 创建 NekoWallpaperItem，grabToImage 截图
- QML 导入路径通过编译时定义传递
- 链接静态 QML 插件，qt_import_qml_plugins 注册

### KPackage 壁纸包

`adapters/plasma/package/`：
- `metadata.json`：KPackageStructure=Plasma/Wallpaper，Id=org.nekomote.wallpaper
- `contents/ui/main.qml`：壁纸主 QML，导入 NekoMote.Wallpaper
- `contents/config/main.qml`：配置 UI（modelFile/sessionFile/origin/scale）

### 构建选项

- `NEKOMOTE_BUILD_PLASMA`（默认 OFF）依赖 `NEKOMOTE_BUILD_QT_QUICK`
- plasma preset：Release + WERROR + Qt + Plasma
- QML 模块目标 `nekomote_plasma_qml`，URI `NekoMote.Wallpaper` v1.0
- Qt 生成代码的 -Wmissing-declarations 仅对生成文件禁用

## 验证结果

| 检查 | 结果 |
| --- | --- |
| Release/WERROR 构建 | PASS（plasma preset） |
| Chocola 截图 vs C7 | SHA 一致 `8711efdb341c3ca9`，黑白合成 0 差异 |
| Fraise 截图 vs C7 | SHA 一致 `47ad767ba84136a1` |
| 缺模型 | status=2, errorString="load model: asset load failed" |
| 缺会话 | status=2, errorString="load session: session asset load failed" |
| 非 OpenGL | 正确拒绝 |
| dev CTest | 145/145 PASS |

## 未完成项

- 1.5 倍 DPI 截图未与 C7 对比（probe 支持 --dpi 但未运行完整对比）
- 重复创建/删除 QML 组件、暂停/释放/重建生命周期未测试
- KPackage 包只读验证元数据/加载方式，未安装
- KDE SDK 缺失情况未记录
- 真实 Plasma 桌面加载未验收（Codex 复核后另作）
- V11 视觉核对交 Gemini

## 交付

源码：`src/host_qt_quick/`、`adapters/plasma/`、CMake/preset 更新
报告：本文件
提交填写实际 Agent/Model/Context=G15-A/B
