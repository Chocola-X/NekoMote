# Plasma 插件分发

目标是每种 CPU 架构构建一份插件，在满足公开 ABI 与运行依赖的 Plasma 6
发行版间复用。不能保证一个本机二进制覆盖全部 Linux：Qt 插件只能由相同 major、
不低于构建 minor 的 Qt 加载；glibc、libstdc++、图形能力也有最低要求。
[Qt 插件部署规则](https://doc.qt.io/qt-6/deployment-plugins.html)明确规定了版本限制。

插件使用宿主 Qt 公共 Quick API，发行包不附带 Qt、KDE Frameworks、glibc、
libstdc++、Vulkan loader 或 GPU 驱动。把另一套 Qt 装进 plasmashell 进程会造成
版本和插件冲突；AppImage/Flatpak 更适合独立进程播放器，不能替代这种进程内插件的
ABI 兼容。Plasma/KF 探针为开发工具，分发构建关闭探针，不把它们的 C++ ABI 带入插件。

PNG 解码另依赖宿主 libpng 1.6（`libpng16.so.16`）及 zlib；基线容器提供开发包，
目标系统依赖由下述 `check-host` 检查，不将这些库复制进插件包。

## 基线构建

[KDE6 工作流](../.github/workflows/plasma6.yml)在 push、pull request 或手动触发时，
分别用 x86_64、aarch64 runner 构建并上传 `nekomote-plasma6-<架构>` artifact。
它独立于 [原生程序工作流](../.github/workflows/native-linux.yml) 和
[KDE5 工作流](../.github/workflows/plasma5.yml)，任一失败不会阻断其他工作流。
动作 SHA 固定；只上传构建产物，不创建 Release，也不安装桌面。
测试、日志、兼容报告及实际工具链包清单见 [自动构建说明](github-actions.md)。

提供 Debian 12 容器配方：Qt 6.4.2、glibc 2.36、GCC 12，使用固定镜像 digest 和
2025-09-01 Debian snapshot；镜像日期早于 snapshot，避免基础 libc 与开发包版本冲突。
构建自身依赖仍沿用仓库的固定 FetchContent 版本，静态依赖许可随包保存。
Qt 6.4 是模块构建基线，使用插件的桌面必须是 Plasma 6；Debian 12 自带 Plasma 5
不在运行目标中。x86_64 与 aarch64 必须分别原生构建或交叉构建，不能共享二进制。

```bash
mkdir -p build/distribution
podman build -f tools/plasma_baseline.Containerfile -t nekomote-baseline tools
podman run --rm --userns=keep-id \
    -v "$PWD:/source:ro" -v "$PWD/build/distribution:/output:rw" \
    nekomote-baseline plasma6
```

Docker 可使用对应的 `docker build/run` 命令；设置 `--user "$(id -u):$(id -g)"`
保持输出所有权。编译不会修改素材、安装路径或桌面配置。

包生成前严格检查所有文件哈希、ELF 架构、相对 RPATH、Qt 私有符号和版本需求；
超过 GLIBC_2.36、GLIBCXX_3.4.30、CXXABI_1.3.13 或 Qt 6.4.2 时拒绝基线包。
归档使用固定文件顺序、所有者及时间戳，不打包模型素材。

已有其他环境的 stage 可生成明确标注实际需求的本地包：

```bash
python3 tools/plasma_distribution.py audit build/plasma/adapters/plasma/stage \
    --qt-version 6.9.1 --output build/distribution/compatibility.json
python3 tools/plasma_distribution.py package build/plasma/adapters/plasma/stage \
    --qt-version 6.9.1 --output build/distribution/nekomote-plasma-local.tar.gz
```

`--qt-version` 必须填实际构建版本。降低该数字不能迁移 Qt 插件；工具也会拒绝
低于 ELF 实际所需 Qt 符号版本的声明。用于广泛分发的包必须通过
`--baseline-debian12`，不能把未通过的 Arch 本机包改名成通用包。

## KDE5 构建与部署

KDE5 有独立 `adapters/plasma5/` 入口，使用 Qt5.12.8 / Ubuntu20 基线；
Qt5 与 Qt6 包不得交叉加载。它复用共享 Qt 绘制桥和 C ABI，不重新实现应用行为。
发行包名为 `nekomote-plasma5-x86_64.tar.gz`，归档结构和下述安装命令相同。
`check-host` 按兼容报告检查 Plasma5、Qt5 及系统依赖；部署工具写入
`QML2_IMPORT_PATH` 并使用 KDE5 的缓存/激活命令。系统 Python3.8 可运行归档的部署工具；
完整源码测试要求 Python3.10+。

本机开发构建可以用：

```bash
cmake --preset plasma5
cmake --build --preset plasma5 --target nekomote_plasma_stage
```

这只构建当前机器的 Qt5 插件，不能代替 Ubuntu20 基线编译。Qt5 的普通窗口诊断宿主
也可构建；现有 KF6/Plasma6 专项探针不支持 Qt5。真实 Plasma5.18 / 5.27 桌面、
OpenGL/Vulkan 共享和 X11/Wayland 接入仍须逐项验收，不能由编译或 QML 导入推定。

## 安装前检查

在目标测试账户中解压可信归档后，先检查，再用统一部署工具安装：

```bash
tar -xzf nekomote-plasma6-x86_64.tar.gz
cd nekomote-plasma
python3 tools/plasma_distribution.py check-host stage
python3 tools/plasma_deploy.py install --stage stage
```

检查不会安装、重启 shell 或修改桌面。它验证归档对应的 Plasma major、CPU 架构、plasmashell
实际解析的 Qt 版本、系统依赖与未定义符号；不使用 `LD_LIBRARY_PATH` 补救不兼容。
仅对可信自身构建执行检查：该步骤使用系统动态加载器 `ldd -r` 检查 ELF。
若发行版移除了 Qt 库文件名中的完整版本，可用 `--host-qt-version` 明确提供
**plasmashell 实际使用的**版本，不能填另一套 SDK 的版本。

安装目录沿用 `<prefix>/nekomote/{qml,lib}`，插件通过
`$ORIGIN/../../../lib` 找到本体；不依赖构建机绝对路径。统一工具提供
`install`、`reinstall`、`uninstall`、`status` 和 `verify` 子命令。安装会自动写入
`$XDG_CONFIG_HOME/environment.d/nekomote.conf`，为 Plasma6 配置 `QML_IMPORT_PATH`（Plasma5 使用 `QML2_IMPORT_PATH`），并刷新
KPackage 缓存；
要立即在当前会话启用，可显式加 `--restart-shell`，让工具重启 plasmashell 并加载新环境：

```bash
python3 tools/plasma_deploy.py install --stage stage --restart-shell
```

不加该选项时，安装不会中断桌面；注销并重新登录后环境变量生效。重装或卸载也可加
`--restart-shell`，避免当前 plasmashell 继续持有旧插件库。卸载只删除安装记录中由工具管理的文件，
并在环境配置仍与工具生成内容一致时移除它。`--prefix` 可选，默认使用 `$XDG_DATA_HOME`，
未设置时使用 `~/.local/share`；普通用户安装不需要额外指定目录。`reinstall` 会识别并清理
旧插件 ID `org.nekomote.wallpaper` 对应的 KPackage 目录，再安装为 `uk.nekopara.nekomote`。
Plasma 中保存的旧壁纸引用仍需要重新选择一次。分发包不自动切换壁纸或复制素材。

## 当前证据与发布门槛

2026-10-12 对C12 双后端本机 stage 的静态核查发现 GLIBC_2.38、GLIBCXX_3.4.31、
CXXABI_1.3.15、Qt_6.9 需求，证明其不满足上述基线。打包、归档完整性、版本拒绝及预检工具在本机
验证；**此环境没有 podman/docker，容器基线编译、GitHub Actions 执行及跨发行版
实机安装尚未执行**。CI 成功后才能提供真实可下载的基线二进制。
这份配方提供可审核的发布入口，不构成已经完成“多数发行版可运行”的承诺。

正式发布仍须保存基线构建日志和 compatibility.json，并在 Qt 6.6/6.8/6.9+
代表桌面上验证 QML 加载、GL/Vulkan 角色/背景/UI、切换、休眠及卸载。
至少覆盖一个 Ubuntu 系桌面、一个 Fedora 系桌面与滚动发行版；X11、Wayland
分别记录。安装前检查通过仅说明加载条件满足，GPU 画面和长期运行仍是独立门槛。
仓库已提供 GNU AGPL-3.0-or-later 的正式 [LICENSE](../LICENSE)，静态依赖许可随包保存；
模型、背景与音频素材不属于插件分发包。
