# GitHub 自动构建与测试

三个工作流独立触发，不互相调用或依赖。任意分支的 push、pull request 和手动
`workflow_dispatch` 都会运行；同一工作流同一 ref 的新运行取消旧运行，不取消其他工作流。
工作流仅上传 Actions artifacts，不发布 Release、不安装或修改桌面。

| 工作流 | 实际编译环境 | Release 产物 |
| --- | --- | --- |
| [native-linux.yml](../.github/workflows/native-linux.yml) | Ubuntu 20.04 / GCC 10.5 / CMake 3.28.4，x86_64 | `nekomote_player`、`nekomote_import`、`nekomote_abi_host` |
| [plasma5.yml](../.github/workflows/plasma5.yml) | Ubuntu 20.04 / Qt 5.12.8 / GCC 10.5，x86_64 | KDE5 / Plasma5 完整壁纸包及原生 QML 模块 |
| [plasma6.yml](../.github/workflows/plasma6.yml) | Debian 12 / Qt 6.4.2 / GCC 12，x86_64 与 aarch64 | KDE6 / Plasma6 完整壁纸包及原生 QML 模块 |

GitHub 当前的 [托管 runner 列表](https://github.com/actions/runner-images#available-images)
不包含 Ubuntu 20.04，因此使用 `ubuntu-24.04` runner 的 Docker 运行 focal 构建。
runner 的系统库不会被用于容器内编译。原生程序和两套插件都开启 Release、LTO、
OpenGL 与 Vulkan，测试失败或依赖审计失败时不上传发行包。

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

- `nekomote-native-ubuntu20-x86_64`：归档内 `nekomote-native/bin/` 仅包含三个请求的程序。
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

## 本次本地验证（2026-10-15）

变更基于 `4d9afc8`，尚未提交。执行环境与结果如下：

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

JUnit 与日志保存在对应本地构建目录。尚未执行 Docker 容器、GitHub Actions、
ARM64 构建及真实 Plasma5/6 插件安装验收；没有修改用户桌面配置。
