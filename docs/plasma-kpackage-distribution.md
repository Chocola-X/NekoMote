# KDE“获取新壁纸”的原生插件分发

2026-11-11，从 `037ece4` 实施。可将NekoMote原生引擎随标准 `Plasma/Wallpaper`
包直接安装，无需安装脚本、全局QML搜索路径或手工复制动态库。

## 依据与实现

只读检查了 `refer/Blue-Archive-Arona-L2D-KDE/` 及用户已安装的
`~/.local/share/plasma/wallpapers/l2d.arona.plana.bluearchive/`：该项目在
`contents/ui/BlueArchiveSpine/` 内放置插件和库，入口使用目录导入。
本次采用这一分发结构，没有复制其源代码、素材或安装脚本。

本机Plasma6的 `/usr/share/knsrcfiles/wallpaperplugin.knsrc` 明确配置：
`Categories=Plasma Wallpaper Plugin6`、`Uncompress=kpackage`、
`KPackageStructure=Plasma/Wallpaper`，且声明原生可执行内容。
KNewStuff由KPackage安装下载的包，不执行现有tar中的Python部署器。

新增 `tools/plasma_distribution.py kpackage`，先沿用既有stage清单、ELF、架构、
Qt major/minor、RPATH、glibc/libstdc++及基线审计，再生成确定性的ZIP：

```text
metadata.json                       # Plasma5另含metadata.desktop
contents/config/main.xml
contents/ui/main.qml
contents/ui/config.qml
contents/ui/NekoMote/Wallpaper/qmldir
contents/ui/NekoMote/Wallpaper/libnekomote_plasma_qmlplugin.so
contents/lib/libnekomote_plasma_qml.so
contents/LICENSE
contents/THIRD_PARTY_NOTICES
contents/compatibility.json
```

两个QML页面都改用包内目录导入；仅调整归档副本，不修改原stage。
插件原有 `$ORIGIN/../../../lib` 在新布局仍指向正确库目录，无需修改ELF。
`compatibility.json` 保留原stage的审计路径和二进制哈希，是构建证据，
运行时检查和安装统一由外层的独立 `installer.py` 负责。

Qt5 和 Qt6.4 都会核验目录 URI，因此导出副本统一删除 qmldir 的 `module` 与
命名模块的 `prefer` 资源重定向，保留两页原来的 1.0 导入版本。插件在 Qt 传入的目录 URI
下注册原生类型；Qt6 的源码/发行版命名导入继续交给生成注册器。
早期 Qt6.9 本机目录导入通过并不能推出 Qt6.4 基线成立；错误和修复证据见下节。
Debug 断言支持合法目录入口，源码 stage 的命名模块保持原状。

## 生成和取得安装包

```bash
python3 tools/plasma_distribution.py kpackage <stage> \
  --qt-version 6.4.2 --baseline-debian12 --package-version 0.5.9 \
  --output nekomote-plasma6-x86_64.wallpaper.zip
# Plasma5对应实际Qt5.12.8、--baseline-ubuntu20。
```

`--qt-version` 必须为实际构建版本，版本和架构不能靠改文件名降级。
stage需已由 `ci_package_licenses.py` 添加许可，再生成manifest。

Plasma5/6 的 x86_64/aarch64 CI 现统一输出 `.tar.gz` 和外层 `.sha256`。
每个 tar 内只有一个带版本的 `.wallpaper.zip`、内层 `.sha256` 和 `installer.py`，
不再分发旧 stage 部署工具。安装器支持安装、同版本重装、卸载和旧分离布局迁移，
具体操作与回滚边界见 [统一分发说明](plasma-distribution.md)。
标签工作流把稳定 tag 版本传入包元数据；普通分支使用 0.0.0。
Release 要求十个 tar.gz 及十个校验文件，共二十个附件；四个 Plasma 包还须通过内层版本、
平台及哈希校验，完整后才发布。GitHub artifact 外层下载 ZIP 中先取出 tar.gz，
提交 KDE Store 的是 tar 里面的 **`.wallpaper.zip`**。

两条下载渠道使用同一份原生实现和完全相同的内层包；商店不需要执行 Python 安装器。

## KDE Store上架步骤与约束

生成正确安装包不会自动让它出现在“获取新壁纸”列表中。维护者还需：

1. 使用KDE Store/兼容OCS服务账户创建壁纸插件条目，填写说明、许可、截图和版本。
2. Plasma6选择对应 `Plasma Wallpaper Plugin6` 类别；Plasma5使用其壁纸插件类别。
   按实际Store界面确认类别，不把普通图片壁纸条目当作插件条目。
3. 上传对应的 `.wallpaper.zip` 或配置指向该文件的稳定下载链接。
   Qt5/6必须分开；x86_64/aarch64也必须让用户明确选对。可以分别建立清楚标注
   major与架构的条目，不能假设KNewStuff会根据ELF自动挑选下载文件。
4. 在目标桌面通过“获取新壁纸”下载、首次选择、配置资产、播放，再验证更新和卸载。

本任务没有登录商店、提交条目或对外发布。因此完成的是**可供KDE原生安装的分发包
及自动构建链**，商店索引可见性和在线下载还未验收。

这些包仍是进程内原生插件：主机必须满足相同Qt major、不低于构建Qt minor及
既有运行依赖；KPackage不会代装系统依赖。模型资产独立维护，不包含在引擎包内，
安装后仍需选择资产目录。原生插件已载入后更新，或从旧tar部署迁移到包内模块时，
需要重启plasmashell或重新登录，避免同一进程继续使用旧模块缓存。

## 初始 KPackage 导出验收（`b136df2`）

- 30项分发/CI打包/Release拒绝和完整清单测试通过，使用实际合成ELF；覆盖确定性ZIP、
  审计失败拒绝、原生库字节保持、Qt5/6 qmldir差异、两个页面导入版本与发布清单。
- 在隔离XDG目录执行真实 `kpackagetool6` 安装成功；随后0.0.1→0.0.2更新成功。
- 本机 `kpackagetool5` 也能解包，但本机缺少Plasma5结构插件，落到通用包根目录。
  **这不作为完整Plasma5壁纸安装验收**，仍需在真实Plasma5桌面测试。
- Qt5.15.17与Qt6.9.2分别新进程运行已提交的
  `tests/plasma/kpackage_import/` 探针：设置页先加载，再创建native item；清空两个
  QML import环境变量，没有添加包外模块目录，两者通过。另删除包内插件做负例，
  两版本均如预期拒绝加载，排除偷偷借用全局模块。
- 对修改后的Qt5 wrapper另做Debug/Werror独立编译并加载，断言开启时也通过。
  本机完整Qt5构建仍缺LinguistTools，本次复用已有后端库，不冒充完整重新构建。
- 本地Qt5归档采用已有基线原生库，Qt6归档采用本机库并声明实际Qt6.9.2；后者不是
  Debian12通用发行包。未执行ARM64、远端CI、在线KNewStuff或真实桌面GPU播放验收。
- 打包与安装不改帧路径，没有新增稳态绘制成本；本轮未运行性能采样。

本地ZIP和完整日志在被忽略的 `experiments/kns-packaging/`；摘要证据在
[验证目录](validation/plasma-kpackage-2026-11-11/)。

复现模块加载：

```bash
cmake -S tests/plasma/kpackage_import -B build/kpackage-import -DNEKOMOTE_QT_MAJOR=6
cmake --build build/kpackage-import
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software QML_IMPORT_PATH= QML2_IMPORT_PATH= \
  build/kpackage-import/kpackage_import <安装后的壁纸包根目录>
```

这是QML/动态库加载验证，不表示已经创建GPU renderer或实际呈现模型。

## 统一外层分发与迁移验收（2026-11-11）

从 `b136df2` 收敛为一个外层 tar：商店 ZIP、内层 SHA256、独立安装器。
旧 stage 安装器和验证器已移除，只在新安装器中保留一次性迁移和卸载识别。
CI 现在从最终归档解包，运行设置页优先和 native 目录导入，Release 校验全部十个外层包，
并核对四个内层壁纸的平台、版本和原生库哈希。

全量 Python 工具检查 180 项、12 项按环境跳过；17 项安装器事务用例通过。
真实隔离 KPackage6 首装、同版本重装、旧 do_install 布局迁移、卸载及资产/配置保留通过。
Qt5/Qt6 的最终内层包目录导入通过。独立复核发现的新包 JSON/desktop 身份回退已修复；
安装器还验证 KPackage 成功后的目标文件，避免返回成功但未安装时提前清理旧文件。

原始日志、环境与哈希见[本轮证据](validation/plasma-unified-distribution-2026-11-11/README.md)。
本轮未改变帧路径，未采样性能；远端基线/ARM64、真实 Plasma5 和在线商店验收仍需分别完成。

## Qt6.4 基线导入补修（2026-11-11）

用户提供的两架构 CI 日志均显示编译和 258 个 CTest runner 通过，失败发生在最终包的
`kpackage_import`：命名模块 `NekoMote.Wallpaper` 与安装目录生成的 URI 不匹配。
该问题影响 Qt6.4 包内加载；本机 Qt6.9 的成功未覆盖这个版本边界。没有移除 CI 检查或提高基线。

仅去掉 module 声明仍会令 `NekoWallpaperItem` 不可见：原 Qt6 插件只提供 engine 初始化，
生成注册器固定使用命名 URI。修复将 Qt6 插件改为 `QQmlExtensionPlugin`，目录 URI 下
显式注册同一原生类型，命名 URI 仍由生成注册器处理；翻译安装保持逐 engine 初始化。
导出时统一去掉命名 module/prefer，stage、Qt5 插件实现及渲染/模型求值均不改。

验证使用 Debian 官方 Qt6.4.2 开发/运行包在项目内解包，配合已有 Actions 的 Qt6.4 后端库：
原包复现失败、仅删声明候选仍失败，重新编译的 Debug/Werror wrapper 加完整修复通过。
最终新 bundle 的基线 ELF/Qt 审计、目录入口、命名入口、三语言（含第二 engine）和缺插件负例
全部按预期完成；本机 Qt6.9 Release/LTO/Werror 重建、四个既有 Qt runner、Qt6.9/Qt5.15
最终包目录导入和 180 项 Python 工具检查（12 项按环境跳过）通过。

这是定向的 x86_64 Qt6.4 加载验证：系统 glibc、C++ 标准库及部分图形依赖来自主机，
没有声称重跑完整 Debian12 容器或 aarch64。两架构远端工作流仍需推送后重跑，
本轮没有改动真实桌面或执行网络发布。证据见
[基线补修记录](validation/plasma6-baseline-import-2026-11-11/README.md)。
