# Arch Linux 系统依赖打包

[PKGBUILD](PKGBUILD) 是 VCS split package 配方，由系统管理依赖版本，不复制 FetchContent
源码。它从项目 Git 仓库构建以下三个包，支持声明为 x86_64 / aarch64：

| 包 | 安装内容 |
| --- | --- |
| `nekomote-git` | `/usr/bin/nekomote_player`、`nekomote_import`、`nekomote_abi_host` |
| `nekomote-layer-shell-git` | `/usr/bin/nekomote_layer_shell` |
| `nekomote-plasma6-git` | 原生库、`/usr/lib/qt6/qml/NekoMote/Wallpaper` 模块及 Plasma6 壁纸包 |

Qt5 不在本配方中；项目 CMake 仍支持 `NEKOMOTE_QT_MAJOR=5`。三个包均可单独安装，
不携带模型、README、测试程序、参考材料、系统动态库或验证 plasmoid。
中文/日文界面可选安装 `noto-fonts-cjk`。

## 构建

需要 Arch 的 `base-devel`，然后在本目录运行：

```bash
makepkg --syncdeps
```

这会拉取远端 Git 源码，并按系统包依赖进行构建、检查和打包，不安装生成的软件包。
远端源码须包含系统依赖构建支持；审查尚未推送的修改时，使用下面的工作树快照方式。

```bash
# 在 packaging/arch 下执行；不创建提交，保留本地未提交代码。
mkdir -p src
git clone --shared --no-hardlinks ../.. src/nekomote
tar -C ../.. --exclude='./.git' --exclude='./build' --exclude='./assets' \
    --exclude='./refer' --exclude='./experiments' --exclude='./node_modules' \
    --exclude='./packaging/arch' -cf - . | tar -C src/nekomote -xf -
makepkg --syncdeps --noextract
```

此路径用于临时验证，不将产生的本地包当作对应 Git 提交的可复现发行物。
`--noextract` 仅适用于已经准备好的源码目录；要重新取远端源码，使用常规构建方式。

配方遵循 [PKGBUILD 规范](https://man.archlinux.org/man/PKGBUILD.5.en)，
启用 `NEKOMOTE_USE_SYSTEM_DEPS`，保留 makepkg 的优化、LTO 和加固参数，关闭项目自己的
Werror，避免发行版的新编译器警告直接阻断用户打包。`check()` 才启用并构建测试，
因此 `--nocheck` 不要求安装 Catch2/Pillow/Node 检查依赖。
CPU/ABI/QML 检查不连接显示服务或声音服务器；GPU/integration 用例明确排除。

三包共用一次 OpenGL/Vulkan 构建。Layer-shell 运行时仍选择 EGL/OpenGL，但因为共用
包含 Vulkan 支持的静态 ABI，本配方生成的 layer-shell 包也声明 `vulkan-icd-loader`
依赖。要构建完全无 Vulkan 的独立壳，可使用项目 `layer-shell` preset 并开启系统依赖；
GitHub 的独立 layer-shell 包保持原有无 Vulkan 配置。

Plasma 使用 Qt6 的系统 QML 搜索目录，RPATH 自动计算到 `/usr/lib/`，不生成
`environment.d` 文件、不要求设置 QML_IMPORT_PATH，也不运行项目的用户目录部署工具。
使用系统包管理器处理安装/卸载，不自动激活或切换桌面壁纸。

项目 LICENSE 安装在 `/usr/share/licenses/<包名>/LICENSE`；实际系统 header-only/生成代码
的依赖许可合并到同目录的 `THIRD_PARTY_NOTICES`。声明从 Arch 已安装的
nlohmann-json、fast_float、stb、glad 等包读取，layer-shell 另包含协议版权文本。
系统共享库通过运行时 depends 提供，依赖包各自维护其许可。

## 本地验证（2026-10-18）

S1，base `cffc7e4`，执行者 Codex，模型为会话提供的 GPT-6 家族，精确变体未知。

宿主 Arch / GCC15.2.1 / Qt6.9.2 / GLFW3.4 / Python3.13。宿主未安装全部开发依赖，
从 Arch 官方 archive 取得与本机包数据库对应的五个包，验证官方签名后只解包到
`build/system-deps/root/`：nlohmann-json3.11.3、fast_float8.0.2、
stb r2205.f056911、glad2.0.8、Catch2 3.8.0。其余库由宿主已安装的开发包提供；
未安装依赖到系统、未修改桌面。临时前缀仅用于验证，不写进源码或配方。

- 系统依赖配置不产生 `_deps`，新版 fast_float8 和 Catch2 3.8 实际参与编译。
  Release/Werror 的软件与必选测试目标构建通过；201/201 无显示 CTest 通过，
  包含自定义系统 QML 安装路径的模块导入。
- 单独全量 Release/O3/Werror 构建 GPU 测试目标时，GCC15 对既有测试中的
  malloc/free 全局 new/delete 计数替换报告 `mismatched-new-delete`。
  没有扩大警告禁用规则或改写 GPU 测试；本次按 CI 的显式必选目标完成严格构建。
  makepkg 使用发行版参数/Werror关闭，完整构建包含 GPU 测试目标，GPU 测试不执行。
- PKGBUILD 在断网 bubblewrap 隔离中以普通用户执行 `makepkg --noextract --nodeps`，
  源码为本工作树快照，依赖从上述临时前缀提供；宿主文件系统只读，仅构建目录可写。
  `--nodeps` 用于此隔离验证，不是推荐给普通用户的构建方式。
- 常规固定版本模式保留，相关播放器/单元/字体目标重建与199/199 CPU CTest通过。
  `.github/` 工作流和 CI 构建脚本未修改。

最终 makepkg 运行也为201/201；三份实际归档的许可证/声明、无 README/验证 plasmoid、
动态依赖解析和相对 RPATH 检查通过。包内 layer-shell 的实际 Vulkan loader 依赖
已在 depends 中声明。makepkg 原先报告的构建源码路径通过编译器 prefix-map 消除，
最终没有该告警。`system` preset 的 Release/LTO/Werror 播放器构建通过；
Qt6 默认归档布局导入通过，RPATH 仍为 `$ORIGIN/../../../lib`。
固定依赖 Qt5 的模块/壁纸入口/配置页两项导入检查通过；缺少本地 Catch2 缓存时首次
配置因沙箱 DNS 失败，随后显式复用项目已有的固定 v3.5.3 源码缓存完成验证。
系统 stb 下五项字体发现/按需回落检查通过，CJK 使用临时的官方 Noto CJK 字体文件，
没有安装字体到系统。

证据保存在 `build/system-deps/`（含 `makepkg-final.log`、`package-audit/report.json`）。
未覆盖 ARM64、Arch 干净 chroot、远端源码获取、其他发行版的系统包或
真实桌面/GPU/长期运行。
未对运行时性能做收益声明。
