# Plasma 插件分发

2026-11-11：Plasma 5/6 统一为自包含 KPackage。GitHub 发布外层 tar.gz，内含
同一份 KDE Store 可上传的壁纸 ZIP、内层 SHA256 和独立安装器；删除旧 stage 分离部署工具。
其他原生程序的分发格式保持原样。完整包结构及商店说明见
[KPackage 分发](plasma-kpackage-distribution.md)。

## 包结构与安装

以 Plasma 6 / x86_64 为例：

```text
nekomote-plasma6-x86_64.tar.gz
└── nekomote-plasma6-x86_64/
    ├── nekomote-plasma6-x86_64.wallpaper.zip
    ├── nekomote-plasma6-x86_64.wallpaper.zip.sha256
    └── installer.py
```

GitHub Release 另附 tar.gz 的 SHA256。Actions artifact 本身还有平台提供的下载 ZIP，
下载后先取出 tar.gz。KDE Store 只上传内层 `.wallpaper.zip`；许可证、第三方声明和
兼容审计报告在内层 `contents/`，不依赖外层文件。

```bash
tar -xzf nekomote-plasma6-x86_64.tar.gz
cd nekomote-plasma6-x86_64
python3 installer.py              # 无参数打开交互菜单
python3 installer.py install      # 首次安装，也可迁移有记录的旧安装
python3 installer.py reinstall    # 同版本重装或更换版本；也支持迁移旧安装
python3 installer.py uninstall
python3 installer.py status
```

这些命令是可选操作示例，按需要执行其中一个。默认自动选择安装器同目录唯一的
`.wallpaper.zip`，可通过 `--package PATH` 明确指定；`--prefix` 指定用户数据根目录，
默认 `$XDG_DATA_HOME` 或 `~/.local/share`。
安装目录是 `<prefix>/plasma/wallpapers/uk.nekopara.nekomote/`。
安装器不需要源码仓库、sudo、网络或第三方 Python 包；需要 Python ≥ 3.8、
对应的 `kpackagetool5/6`、`plasmashell` 和 `ldd`。应在目标用户账户执行。

安装前验证内层 SHA256、ZIP 路径和类型、插件身份、兼容报告及两个原生库的哈希，
再核对 CPU 架构、Plasma major、plasmashell 实际链接的 Qt major/minor，执行 `ldd -r`
检查依赖。仅对可信发行包使用：哈希用于检测损坏，不证明来源；`ldd -r` 会使用系统加载器。
如果 Qt 的真实库文件名无法提供完整版本，安装器明确失败，不猜测另一套 SDK 的版本。

安装通过原生 KPackage 完成，两页 QML 使用包内模块，不写入全局 QML 搜索路径。
安装器不自动重启 shell 或更换壁纸。更新/迁移后请注销并重新登录，释放旧模块与旧环境；
然后在桌面壁纸设置选择 NekoMote 并指定资产目录。
模型资产、共享面板状态及桌面配置不属于卸载范围。

## 旧安装迁移和失败回滚

新安装器只保留旧方式的识别、卸载与迁移代码，不再提供旧布局安装：

- 识别 `<prefix>/nekomote/.install-record.json`，只接受预定库、QML 模块、许可证、
  当前壁纸 ID 和验证 plasmoid 目录下的记录路径。禁止越界路径和符号链接祖先。
- 在任何 KPackage 操作或删除前核对所有旧文件哈希；旧目录出现未记录文件或用户修改
  时停止。由旧工具生成的 `environment.d/nekomote.conf` 只在路径、哈希或精确旧内容
  匹配时清理，不根据记录中的任意外部路径删除文件。
- 更早的 `org.nekomote.wallpaper` 目录须由 metadata 证明插件身份和壁纸类型。
- 先完成新包和主机预检，再备份现有包及旧文件。重装经 KPackage 移除再安装，支持
  同版本；成功后清理旧分离库和记录。任一受检查操作失败时恢复备份中的文件字节和权限。

这是进程内失败的文件事务回滚，不承诺断电、进程被强制终止或磁盘故障时自动恢复。
当前 plasmashell 已载入的共享库不会因文件回滚立即卸载，仍应重新登录。
没有旧记录且无法证明身份的遗留文件不会被猜测删除；请先人工确认归属。

## 基线与兼容范围

| 包 | 构建基线 | 符号上限 |
| --- | --- | --- |
| Plasma 5 / Qt 5 | Ubuntu 20.04、Qt 5.12.8、GCC 10.5 | GLIBC 2.31、GLIBCXX 3.4.28、CXXABI 1.3.12 |
| Plasma 6 / Qt 6 | Debian 12、Qt 6.4.2、GCC 12 | GLIBC 2.36、GLIBCXX 3.4.30、CXXABI 1.3.13 |

x86_64 与 aarch64 分别构建。Qt5/6 不得交叉加载；Qt 插件要求相同 major、主机
minor 不低于构建 minor。包不附带 Qt/KDE、glibc、libstdc++、图形驱动或 Vulkan loader，
不链接 KF/Plasma C++ ABI。PNG/zlib、PulseAudio、Fontconfig 和图形库仍由宿主提供，
实际依赖见内层兼容报告。Debian12 仅用于编译 Qt6 模块，其自带 Plasma5 不是 Qt6 包的运行目标。
安装前检查通过不等于 GPU、跨发行版或长期播放验收。

## 构建与打包

CI 的 [Plasma5](../.github/workflows/plasma5.yml)、
[Plasma6](../.github/workflows/plasma6.yml) 各构建两个架构，沿用固定基线容器和依赖。
`tools/ci_build.sh` 完成构建、必选测试、许可证合并、stage manifest、ELF/Qt/基线审计，
再生成一个统一外层包，并重新验证内层版本、平台、哈希和布局。
分支包版本为 `0.0.0`；稳定标签将 `vX.Y.Z` 转为 KPackage 的 `X.Y.Z`。
发布工作流要求十个 tar.gz 与十个校验文件完整；四个 Plasma 包另外校验内层版本与发布标签一致。

本机 Plasma6 源码构建和打包示例：

```bash
cmake --preset plasma -DNEKOMOTE_BUILD_PLASMA_PROBE=OFF
cmake --build --preset plasma --target nekomote_plasma_stage
mkdir -p build/distribution
python3 tools/ci_package_licenses.py . build/plasma build/plasma/adapters/plasma/stage plasma6
python3 tools/plasma_stage_manifest.py build/plasma/adapters/plasma/stage
python3 tools/plasma_distribution.py bundle build/plasma/adapters/plasma/stage \
  --qt-version "$(qmake6 -query QT_VERSION)" --package-version 0.5.9 \
  --output build/distribution/nekomote-plasma6-x86_64.tar.gz
```

Plasma5 对应 `plasma5` preset、stage 路径、许可证模式和 `qmake`。
`--qt-version` 必须是实际构建版本，架构也必须匹配；不要把本地新 Qt 包改名冒充低版本基线包。
正式分发需分别添加 `--baseline-ubuntu20` 或 `--baseline-debian12`，超过上限会拒绝生成。
`kpackage` 子命令可以单独导出商店 ZIP，但 GitHub CI 只发布包含它的统一外层包。
历史报告里的 `plasma_deploy.py` 和 `check-host stage` 命令已由安装器替代。

验证记录、覆盖范围与待验收项见 [本轮记录](plasma-kpackage-distribution.md)。
