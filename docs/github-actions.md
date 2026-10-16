# GitHub 自动构建与测试

三个日常构建工作流独立触发：任意分支的 push、pull request 和手动
`workflow_dispatch` 都会运行；同一工作流同一 ref 的新运行取消旧运行，不取消其他工作流。
它们也提供 `workflow_call`，供独立的标签发布工作流复用。分支 CI 只上传 Actions
artifacts；版本标签统一由发布工作流触发构建，不额外重复运行三套 push CI。

| 工作流 | 实际编译环境 | Release 产物 |
| --- | --- | --- |
| [native-linux.yml](../.github/workflows/native-linux.yml) | Ubuntu 20.04 / GCC 10.5 / CMake 3.28.4，x86_64 与 aarch64 | `nekomote_player`、`nekomote_import`、`nekomote_abi_host` |
| [plasma5.yml](../.github/workflows/plasma5.yml) | Ubuntu 20.04 / Qt 5.12.8 / GCC 10.5，x86_64 | KDE5 / Plasma5 完整壁纸包及原生 QML 模块 |
| [plasma6.yml](../.github/workflows/plasma6.yml) | Debian 12 / Qt 6.4.2 / GCC 12，x86_64 与 aarch64 | KDE6 / Plasma6 完整壁纸包及原生 QML 模块 |

GitHub 当前的 [托管 runner 列表](https://github.com/actions/runner-images#available-images)
不包含 Ubuntu 20.04，因此使用 `ubuntu-24.04` runner 的 Docker 运行 focal 构建。
runner 的系统库不会被用于容器内编译。原生程序和两套插件都开启 Release、LTO、
OpenGL 与 Vulkan，测试失败或依赖审计失败时不上传发行包。

## 版本标签自动发布

[release.yml](../.github/workflows/release.yml) 在推送 `vX.Y.Z` 格式的稳定版本标签时
构建并发布，例如 `v0.5.0`。`v*` 事件会先校验完整版本格式；`v0.5`、`v01.5.0`、
预发布标签等不符合当前规则的标签会失败，不进入构建和发布。

先提交并推送需要发布的源码，再为该提交打标签并推送：

```bash
git push
git tag -a v0.5.0 -m "NekoMote v0.5.0"
git push origin v0.5.0
```

标签必须指向包含发布工作流的提交；标签本身是发布版本，不需要单独更新 CMake 中的
内部工程版本。发布工作流使用该标签提交内的三个可复用工作流及构建脚本，等待全部
矩阵任务成功，收集当前 run 的五份归档与各自的 `.sha256` 文件：

| Release 附件 | 内容 |
| --- | --- |
| `nekomote-native-ubuntu20-x86_64.tar.gz` | Intel/AMD 64 位 Linux 的三个原生程序 |
| `nekomote-native-ubuntu20-aarch64.tar.gz` | ARM64 Linux 的三个原生程序 |
| `nekomote-plasma5-x86_64.tar.gz` | KDE5 插件 |
| `nekomote-plasma6-x86_64.tar.gz` | KDE6 x86_64 插件 |
| `nekomote-plasma6-aarch64.tar.gz` | KDE6 ARM64 插件 |

本体 ARM64 和 KDE6 ARM64 均使用 `ubuntu-24.04-arm` 原生 runner，在相应架构的
固定基线镜像中编译。focal 的 Python / Node 工具下载按架构选择固定 URL 与 SHA256；
不使用 x86 模拟器，不将这些构建工具捆绑进发行包。

发布前检查附件齐全且 SHA256 一致；之后创建 draft、上传全部附件，上传成功后转为
正式 Release 并设为 Latest，说明由 GitHub 自动生成。构建或校验失败不创建 Release；
上传失败保留 draft，可从 Actions 重跑。已经公开的同版本不会被覆盖，应使用新标签。
Release 页面可直接下载归档；Actions 中继续保留各平台测试、审计和失败日志。

仅发布 job 获得 `contents: write`，构建 job 仍为只读。使用内置 `GITHUB_TOKEN`，
无需配置额外 PAT，也不安装或修改桌面。工作流配置依据 GitHub 的
[可复用工作流](https://docs.github.com/en/actions/how-tos/reuse-automations/reuse-workflows)
及 CLI 的 [创建 Release](https://cli.github.com/manual/gh_release_create) /
[发布 draft](https://cli.github.com/manual/gh_release_edit) 契约。

## 工具链与源码边界

Ubuntu20 镜像固定 digest，使用官方 focal 包中的 GCC10；系统自带 GCC9 与
CMake3.16 不满足项目要求。CMake 和 Python 构建工具另行固定版本；独立 Python3.11
下载校验 SHA256，仅用于构建和测试，不随本体分发。focal apt 包实际版本保存在
`toolchain-packages.txt`，APT 仓库没有冻结快照，不宣称每次镜像重建逐字节一致。

PulseAudio 最低开发版本调至 13.99，符合现有 mainloop/context/stream API 的使用。
WAVE 的两处位解释使用 `memcpy`，保留样本位模式与别名安全，避免 GCC10 标准库缺少
`std::bit_cast`；不添加双套解码实现。该变化只在音频资产加载路径，不改稳态帧路径。

GCC10 也缺少浮点 `std::from_chars`。模型 blank domain 浮点解析使用固定版
fast_float v6.1.6（header-only，选择 MIT 授权，许可随包保留），保持严格、无 locale
依赖的数字解析，不添加旧编译器专用的另一套公式。JSON 和 fast_float 的第三方头文件
作为 system includes 处理；项目自己的警告仍作为错误。

KDE5 使用独立 `adapters/plasma5/`，包括 Qt5 插件注册、带版本的 QML imports、
Plasma5 注入的 `wallpaper.configuration` 和旧 `FileDialog`。KDE6 继续使用
`adapters/plasma/` 的 `WallpaperItem` 和 `FolderDialog`。Qt major 通过
`NEKOMOTE_QT_MAJOR=5/6` 显式选择，不自动探测回退。

两者共享原生 item、Qt 绘制桥、C ABI 和壁纸应用层，没有复制角色、UI 或声音策略。
共享桥仅对输入坐标 API、GL 状态恢复和诊断通知做 Qt major 条件编译。
Qt6 继续用 `afterFrameEnd`；Qt5 诊断用 `afterRendering`，不能当成实际呈现时间。
Vulkan GPU 图像共享同步保持原有契约。Release 警告检查还促使两处纹理查找在
解引用前显式报告丢失资源，而不是允许空指针继续绘制；未宣称其性能收益。部署工具按平台使用 `QML2_IMPORT_PATH` /
`QML_IMPORT_PATH` 和 KDE5 / KDE6 缓存及激活工具。

## 测试与下载

进入仓库 Actions，选中成功的运行，在 Artifacts 下载对应包：

- `nekomote-native-ubuntu20-x86_64` / `nekomote-native-ubuntu20-aarch64`：归档内
  `nekomote-native/bin/` 仅包含三个请求的程序。
- `nekomote-plasma5-x86_64`：Qt5 插件，不得装进 Plasma6。
- `nekomote-plasma6-x86_64` / `nekomote-plasma6-aarch64`：Qt6 插件，不得装进 Plasma5。
- `evidence-*`：日志、JUnit `tests.xml`、兼容报告、包版本与未执行测试清单；失败也保存。

每个工作流运行合成模型的 CPU/runtime/application/UI、C ABI 正负边界以及 Python
工具测试。插件另从安装后的 stage 目录加载 QML 模块，验证注册、相对 RPATH、默认状态
和非法后端拒绝；Qt5 额外加载壁纸入口与配置页，验证其注入上下文。

`gpu` / `integration` 标记的测试不在这些任务中执行，列入 `excluded-tests.txt`；
缺失真实素材不记为通过。容器没有真实桌面，也没有 Plasma6 QML 包，因此 Qt6 模块导入
不能证明整个 KDE6 页面或桌面插件已通过。GPU 画面、驱动上的 GL/Vulkan 共享、
真实 Plasma5/6 安装、X11/Wayland、声音及长期运行仍需独立验收。

## 二进制兼容范围

原生程序与 KDE5 插件严格检查符号需求不超过 GLIBC2.31、GLIBCXX3.4.28、
CXXABI1.3.12；KDE5 构建 Qt 不超过5.12.8。原生程序另在容器内执行 `ldd -r`，
检查传递的系统依赖能够解析。归档不捆绑系统库，目标仍需安装 OpenGL、Vulkan loader、
PulseAudio、PNG/zlib、Fontconfig 及相关窗口依赖，可从兼容报告查看实际依赖。

KDE6 基线为 Qt6.4.2、GLIBC2.36、GLIBCXX3.4.30、CXXABI1.3.13。
这比当前开发机的 Qt6.9 降低了构建下限，但不覆盖旧 glibc 或所有驱动。
Qt 插件必须由相同 major、不低于构建 minor 的 Qt 加载，见
[Qt 插件部署规则](https://doc.qt.io/qt-6/deployment-plugins.html)。不链接 Plasma/KF
C++ ABI、不附带另一套 Qt，不能将其称为覆盖全部 KDE6 桌面的通用二进制。
安装步骤见 [Plasma 分发](plasma-distribution.md)。

部分 Qt5 构建将公开的 `QQuickFramebufferObject::Renderer` 导出标记为
`Qt_5_PRIVATE_API`。已对照 Qt 官方 [5.12 头文件](https://github.com/qt/qtdeclarative/blob/5.12/src/quick/items/qquickframebufferobject.h)
与 [5.15 头文件](https://github.com/qt/qtdeclarative/blob/5.15/src/quick/items/qquickframebufferobject.h)，
其公开 Renderer 声明一致。审计仅对当前使用的五个公开符号允许该 Qt5 标签，并在
`public_qt5_renderer_exports` 中记录；其他 Qt5 私有实现及所有 Qt6 私有符号仍拒绝。
这项静态检查不代替跨 Qt5 minor 的实际加载与桌面验收。

项目核心库以静态库参与最终链接；Qt、libstdc++、PulseAudio、PNG 等系统库由宿主提供。
CI 检查三个程序和插件本体的 `DT_NEEDED`，拒绝意外静态链接 libstdc++。
本地曾将 GCC10 deb 解包到临时目录做兼容编译，缺失动态库链接目标导致它选中
`libstdc++.a`、文件变大；已经修正本地链接路径。这些混合本机 SDK 的产物不是
Ubuntu20 分发包，不用于冻结基线符号需求。

## 本地复现容器构建

```bash
mkdir -p build/ci-native20 build/ci-plasma5 build/ci-plasma6
docker build -f tools/ci_ubuntu20.Containerfile -t nekomote-focal tools
docker run --rm --user "$(id -u):$(id -g)" \
  -v "$PWD:/source:ro" -v "$PWD/build/ci-native20:/output:rw" nekomote-focal native20
docker run --rm --user "$(id -u):$(id -g)" \
  -v "$PWD:/source:ro" -v "$PWD/build/ci-plasma5:/output:rw" nekomote-focal plasma5
docker build -f tools/plasma_baseline.Containerfile -t nekomote-bookworm tools
docker run --rm --user "$(id -u):$(id -g)" \
  -v "$PWD:/source:ro" -v "$PWD/build/ci-plasma6:/output:rw" nekomote-bookworm plasma6
```

本地没有 Docker/Podman 时不能将普通本机构建当成基线容器验收；首次 Actions 成功后
才有对应的基线产物可下载。

## 首次交付的本机验证（2026-10-15）

以下是 `bf91d0b` 提交前、基于 `4d9afc8` 的本机验证记录。首次 Actions 实际运行已失败，
这组混合本机 SDK 的结果不足以验收基线构建；后续修复和隔离基线验证见下一节。
当时执行环境与结果如下：

| 检查 | 实际环境与结果 |
| --- | --- |
| KDE5 与三个程序编译 | GCC10.5，Release + LTO + Werror；三个程序、Qt5 插件与测试目标编译通过 |
| KDE5 核心/ABI/工具/QML | 203/203 CTest 通过，包括安装目录模块加载、壁纸入口和配置页加载 |
| KDE6 编译与回归 | 本机 GCC15.2、Qt6.9.2、Debug + Werror；插件与诊断宿主编译通过，202/202 CTest 通过 |
| 分发/部署工具 | 16 项 Python 检查通过；两个归档工具通过 Python3.8 语法检查 |
| Vulkan 定向回归 | 3 个合成用例通过：绘制、遮罩复用、预热帧分配；使用自有隐藏窗口 |
| 工作流与脚本 | actionlint v1.7.12、`bash -n`、`git diff --check` 通过 |
| 独立复核 | 只读复核完成，未发现剩余确定的 CI 阻塞；没有代替容器执行 |

GCC10 本地工具链从官方 focal deb 解包，但使用本机 glibc、Qt5.15.17 与动态
libstdc++，不是完整 focal sysroot。KDE6 本地测试也不是 Debian12 基线。
两套本地 stage 的 ELF 审计和 KDE5 打包通过，未使用严格的基线验收开关；这些结果
只证明当前构建和打包链路能运行，不能证明归档满足 Ubuntu20 / Debian12 的符号上限。

本地三个程序均已确认动态链接 libstdc++。Release/LTO 文件大小约为 player 1.6 MiB、
ABI host 1.6 MiB、importer 574 KiB；之前临时工具链误选静态 libstdc++ 时约为
3.5 MiB、3.5 MiB、2.2 MiB。这里仅记录二进制大小，不作为运行性能结论。

CTest 的 CPU 检查使用 `-LE 'gpu|integration'`；Python 工具集合在 CTest 中计为一个
测试，不能将集合内部用例数重复累加。Vulkan 定向检查另行执行：

```bash
ctest --test-dir build/ci-focal-toolchain -LE 'gpu|integration' --output-on-failure
ctest --test-dir build/ci-plasma5-local -LE 'gpu|integration' --output-on-failure
build/ci-focal-toolchain/bin/nekomote_vk_tests \
  'Vulkan warmed model and UI frame paths reuse CPU allocations,Vulkan five-bit mask union reuses descriptors beyond original exhaustion point,Vulkan target clear preserve and top-left atlas coordinates match OpenGL'
```

第二个构建目录沿用早期名称，最终配置实际为 Qt6。Vulkan 首次执行因沙箱无法连接
图形环境而失败，获准在沙箱外执行后上述三个用例通过。零分配只覆盖测试跟踪的渲染线程
普通 C++ 分配，不含驱动 malloc、其他线程或整个壁纸应用，也未做帧时间性能比较。

JUnit 与日志保存在对应本地构建目录。当时尚未执行 Docker 容器、GitHub Actions、
ARM64 构建及真实 Plasma5/6 插件安装验收；没有修改用户桌面配置。

## 首轮 Actions 失败修复（2026-10-15）

用户提供了 `bf91d0b` 的四个 job 日志，三个工作流均在编译/链接阶段失败：

| Job | 首个实际错误 | 修复 |
| --- | --- | --- |
| Ubuntu20 native | `lto-wrapper: fatal error: execvp: No such file or directory` | 两个镜像显式安装 GNU make；Ninja 外层构建不能替代 GCC 并行 LTO 调用的 make |
| Plasma5 | Qt5.12 缺少 `PFNGL*EXTPROC` 和外部内存/信号量常量 | Qt 桥使用系统 Khronos `GL/glext.h` 补齐声明，保留原运行期扩展检查、函数解析和同步 |
| Plasma6 x86_64 / aarch64 | `test_image.cpp` 范围循环复制触发 `-Werror=range-loop-construct` | 测试循环绑定 `const auto&`，不降低警告门槛 |

继续复核发现两个镜像缺少必选 Python 工具测试的 Pillow 和 Node。focal 的独立
Python 安装固定 Pillow11.1.0，另安装带 SHA256 校验的 Node22.14.0；Debian12 使用
快照中的 `python3-pil` / `nodejs`。这些仅为构建/测试工具，不捆绑到发行包。
源码快照排除本地调查和实验输出，编译前检查并记录 make、Python/Pillow/Jinja2、Node
及 C/C++ 编译器，缺失时提前失败。真实 ELF 工具测试遵循 `CC`，避免 focal 仅安装
`gcc-10` 而没有 `cc` 时静默跳过整类测试。

上轮本机 SDK 包含较新的 GL 扩展头、make、Pillow 和 Node，未暴露这些基线环境缺项。
本轮保持 Release、LTO、Werror 和原版本下限，使用固定基础镜像内的完整工具链重新验证。

完整 focal 构建还复现了单元测试链接 `pthread_create` 失败：application 的
`std::async` 没有声明线程依赖，之前被本机环境掩盖。依赖补在实际使用它的
`nekomote_application` 目标，采用 `Threads::Threads`，不向全部目标添加全局链接参数。

后续实际编译/测试还修正了以下问题：

- GCC12 对 Qt 桥的信号量配对循环及两个几何测试循环发出同类复制警告，均改为只读引用。
- Qt5 的 `FileDialog` 依赖 Controls1、`Qt.labs.folderlistmodel` 和 `Qt.labs.settings`，
  镜像补齐这些模块并保留 Controls2；没有删去配置页测试。
- 合成 cc 探针提供 `--source-commit` 参数，归档测试显式传入合成提交标识，并从临时工作目录运行；
  正式 Git 工作区仍读取工具所属仓库的真实 HEAD，不创建伪 Git 仓库或用未知版本冒充来源。
- Catch3.5.3 的标签不会自动成为 CTest labels。按 `TEST_SPEC` 分别发现普通测试和
  `integration` 测试，素材缺失统一 SKIP；消除五处 Controller 和一处真实导入的早退假 PASS。
  CTest 用精确的单用例全跳过报告识别 Skipped，不直接把返回码4当跳过，避免掩盖四次断言失败。

现在必选 CPU 检查与真实素材/字体集成检查分开统计；首次本机记录中的历史测试总数不能
直接当作新版 CI 合成测试通过数。Python 集合内部的可选 GPU/宿主检查也保留其 Skipped 状态。

### 修复后的隔离基线验证

源码为 `bf91d0b` 加本轮修复，全部采用 Release + LTO + Werror。本机没有
Docker/Podman；下载并校验 Containerfile 指定的官方 OCI 镜像，以完整的 subordinate
UID/GID 映射和 bubblewrap 隔离执行配方、同一个 `tools/ci_build.sh`。编译器、头文件、
链接器、Qt 和系统库均来自镜像，源码只读挂载，未混入本机 SDK；容器内为 root，
没有显示服务，也没有安装或修改用户桌面。此验证不等于 Docker 引擎或 Actions runner 验收。

| 模式 | 实际工具链 | 必选 CTest | 打包与审计 |
| --- | --- | --- | --- |
| native20 x86_64 | Ubuntu20 / GCC10.5 / glibc2.31 | 191/191 通过 | 三个程序归档、动态 libstdc++、`ldd -r` 和严格符号上限检查通过 |
| plasma5 x86_64 | Ubuntu20 / GCC10.5 / Qt5.12.8 / glibc2.31 | 193/193 通过 | stage 模块、壁纸入口/配置页加载、manifest、许可、`--baseline-ubuntu20` 及归档通过 |
| plasma6 x86_64 | Debian12 / GCC12.2 / Qt6.4.2 / glibc2.36 | 192/192 通过 | stage 模块加载、manifest、许可、`--baseline-debian12` 及归档通过 |

CTest 的上述数量包含一个 Python 工具集合，不再累加集合内部的用例数；真实素材、字体和
GPU 集成用例按 labels 排除并另存清单，不计为通过。原生三个程序实际符号需求的最大值为
GLIBC2.29 / GLIBCXX3.4.28 / CXXABI1.3.11，KDE5 插件相同；KDE6 插件为 GLIBC2.34 /
GLIBCXX3.4.29 / CXXABI1.3.13，均满足各自审计门槛。这些不证明跨发行版或 GPU 行为兼容。

已完成归档的 SHA256（仅对应本次本地快照，不保证与后续 CI 字节一致）：

```text
2f13675fdf7cb2f6c4227a42a975c60dce877735223ce927bbbb0c8710c9bf5c  nekomote-native-ubuntu20-x86_64.tar.gz
b214c68cf365490feb4e9ce289c112120efb4b0cf2b45ea6121bb6b6dd567be5  nekomote-plasma5-x86_64.tar.gz
91271ff63bc1795eedc560b75e33320a93c8d3bad3e5b8cb200981be48f33151  nekomote-plasma6-x86_64.tar.gz
```

本轮未执行 ARM64、修复后的 GitHub Actions 或真实 Plasma 桌面运行，也没有运行帧时间、
功耗或长期基准；未改变 GPU 算法和同步协议，不声称有运行性能收益。

## 用户验收与标签发布配置（2026-10-15）

用户随后反馈：已推送运行 GitHub CI，并验证构建产物中的本体、KDE6、KDE5 均正常，
授权配置自动发布和提交。此为用户实际使用反馈；未提供 run 链接、逐项架构、驱动和
长期覆盖，不扩展为全部设备验收。

本次新增标签编排、发布脚本和本体 ARM64 矩阵，保持既有测试/严格审计入口。
发布脚本的离线回归覆盖完整附件发布顺序、draft 重试、公开版本拒绝覆盖、上传失败、
归档损坏、缺失/多余文件与非法版本；测试使用假的 `gh`，不会创建真实 Release。
本地 `actionlint v1.7.12`、Shell 语法、Containerfile RUN 的 POSIX Shell 语法、
`git diff --check` 及 7/7 离线发布测试通过，并完成独立只读复核。测试入口为
`python3 -m unittest tests.reference_capture.test_ci_release -v`。
发布工作流尚未触发，本体 ARM64 也未在本地执行编译；首次远端运行仍需检查结果。
本次仅改构建/发布与文档，不影响运行时渲染或帧路径性能。

## 新增音频设备测试后的 CI 修复（2026-10-17）

`4183c24` 新增了 `audio_device_contract`，但遗漏同步 `tools/ci_build.sh` 的显式
`TARGETS` 清单。用户提供的 native20 x86_64 日志显示构建成功，197 项测试通过，
唯一未运行项因缺少 `nekomote_audio_device_tests` 可执行文件而使 CTest 返回失败。
JUnit 将此项记为 `notrun/skipped`，不能仅据其 `failures="0"` 宣称 CI 通过。

修复将该可执行目标加入 native20、plasma5、plasma6 共用的构建清单，保留测试及
原有失败门槛。后续新增独立必选测试可执行文件时，必须同时更新此清单。
本地 Shell 语法检查、四个共同测试目标构建，以及音频设备/ABI 负例/ABI 合成边界
三项 CTest 通过；本轮未重新执行基线容器、远端 Actions 或 ARM64 验证。
