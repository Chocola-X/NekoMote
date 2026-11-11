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
不能把原来的 `check-host stage` 命令直接应用于解压后的KPackage目录。

Qt6的命名qmldir可直接用于目录导入；Qt5会校验目录URI，因此导出时删除Qt5
qmldir的module声明，保留两页原来的1.0版本，Qt5注册器接受Qt传入的目录URI。
Debug断言也支持这一合法入口，原tar的命名模块仍按原方式注册。

## 生成和取得安装包

```bash
python3 tools/plasma_distribution.py kpackage <stage> \
  --qt-version 6.4.2 --baseline-debian12 --package-version 0.5.9 \
  --output nekomote-plasma6-x86_64.wallpaper.zip
# Plasma5对应实际Qt5.12.8、--baseline-ubuntu20。
```

`--qt-version` 必须为实际构建版本，版本和架构不能靠改文件名降级。
stage需已由 `ci_package_licenses.py` 添加许可，再生成manifest。

Plasma5/6的x86_64/aarch64 CI各额外输出 `.wallpaper.zip` 和 `.sha256`。
标签工作流把稳定tag版本传入包元数据；普通分支产物使用0.0.0供开发检查。
Release现在要求10份原tar、4份壁纸ZIP及14份校验文件，共28个附件完整后才发布。
GitHub下载artifact外层ZIP包含多个附件；提交KDE Store的是里面的
**`.wallpaper.zip`**，不是artifact外层ZIP或原部署tar。

已有tar部署继续用于系统依赖预检和命令行管理，这两种归档复用同一套原生实现。
不需要另一个Web播放器、shell下载器或第二套模型业务。

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

## 本轮验收

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
