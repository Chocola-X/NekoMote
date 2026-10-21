# CFG1：启动配置与面板偏好分离

2026-10-21，从 `e72e3fd` 开始。按用户要求，在 X11 / layer-shell Vulkan 交付后，
统一 player、独立桌面壳和 Plasma 的配置责任。使用方式及离线迁移见
[配置说明](configuration.md)，公共边界见 [ABI 契约](abi-contract.md)。

## 实现与责任

- `application::LaunchConfig` 定义启动 schema、默认位置、校验和显式保存；独立壳通过
  新增 C ABI 调用。ABI 升为 0.7.0，仓库内调用者和版本检查同步迁移。
- `--config FILE` 选择启动配置，`--state FILE` 选择面板文件，`--save-config` 合并
  已有文件与显式参数，保存后退出，不初始化显示。普通运行只读取启动文件，命令行优先。
- 未指定资产时不推定目录，不创建或复制资产库。默认根目录使用绝对
  `$XDG_CONFIG_HOME/nekomote`，否则使用绝对 `$HOME/.config/nekomote`。
- 独立实例默认使用 `<启动文件 stem>.panel.json`。派生路径不序列化进启动文件，复制
  配置后自然分离；显式自定义 `state_path` 保留。输出名称不再生成配置标识。
- 面板 schema、默认值与保存时机仍由共享 application / UI 管理。保存角色、背景、
  语言、FPS、静音、位置锁定、变换、布局和背景渐变时间。输出尺寸、临时暂停和面板
  展开状态不属于偏好。运行时 CLI 静音等临时选项不改写偏好。
- 删除初次背景确认、自动日夜背景切换、自动适配、尺寸恢复、布局恢复和退出时的默认值
  写入。用户选择背景成功后才保存；用户背景请求优先于自动时段刷新。
- 两类文件均为严格 version 1 JSON；未知字段、重复键、错误类型、非法数值和嵌入 NUL
  明确拒绝。启动与面板路径不得相同，包含符号链接指向同一路径的情况。
- 写入使用同目录独占临时文件、完整写入与 `fsync` 后原子替换；失败保留原文件并报告。
  未存在的配置只返回内存默认值，不为读取创建目录。资产目录中的旧配置不再探测。

旧配置通过 `tools/migrate_wallpaper_config.py` 显式离线迁移，只迁移面板数据并移除
旧尺寸字段。目标必须不存在；源文件不修改。没有对用户当前资产或桌面执行迁移。

## Plasma 实例与生命周期

Plasma 5 / 6 在各自壁纸 KConfig 中保存 `panelInstanceId` UUID；启动后端和资产路径
仍由插件管理。面板路径为 `nekomote/plasma/<UUID>.json`，不依赖屏幕编号或输出名称。
QML 在 UUID 就绪后才提交资产路径，避免提前读取共享默认面板。Qt 同步快照携带配置
路径，仅在新建壁纸句柄时调用设置路径 ABI；配置切换随 renderer 重建处理。

验收曾发现 Qt 6 的属性映射修改只存在于内存；已补充 `configuration.writeConfig()`。
实际 KPackage 探针现在通过独立打开磁盘 KConfig 验证 UUID，而非仅检查原有属性映射。
Qt 5 对提供该方法的宿主显式写入，旧版 KDeclarative 使用原有自动保存契约。
手工复制整份 Plasma 配置会复制 UUID；创建独立实例时应清空该字段。

Vulkan → OpenGL → Vulkan 重建检查还暴露 NVIDIA 驱动销毁 Vulkan instance 后扰动
原生 GL 绑定的问题。Qt 桥在 teardown 后恢复调用者的当前 surface，失败记录明确诊断；
不引入业务状态机或新的图形资源。该处已由另一执行者复核，并通过重建检查。

## 验证

本机 x86_64、Linux `6.12.39-1-MANJARO`，NVIDIA GeForce RTX 3060 Laptop GPU /
575.64.03。三个构建目录使用 Release、LTO 和 Werror；Qt 5 / Qt 6 使用本机依赖，
不冒充发行基线镜像验证。源码、ABI、独立壳和 Qt 生命周期由其他执行者交叉复核。

| 验证入口 | 结果与覆盖 |
| --- | --- |
| `ctest --test-dir build/vulkan-release -LE 'gpu\|integration' --parallel 6 --output-on-failure` | 最终 239/239 个 CTest runner 通过，含配置/ABI/Controller/UI、壳参数、player 子进程和 Python 工具；该数量不是单元断言数 |
| Qt 6：`QT_QPA_PLATFORM=offscreen ctest --test-dir build/plasma -R '^(plasma_module_import\|plasma_config_reject)$' --output-on-failure` | 2/2，通过模块、实例标识和配置拒绝检查 |
| Qt 5：`QT_QPA_PLATFORM=offscreen ctest --test-dir build/plasma5 -R '^(plasma_module_import\|plasma5_package_import)$' --output-on-failure` | 2/2，通过模块和实际包 QML 绑定检查 |
| 私有 KWin / XWayland X11 harness | 57/57 项通过，含双后端配置恢复；三对角色/资产/UI 和一对 ABI 客户端图像一致，Vulkan validation 0 errors / 0 warnings |
| 私有 KWin layer-shell OpenGL harness | 最终二进制 21/21 项通过，含配置恢复、资产/UI、不同输出尺寸、固定 scale、退出及失败路径 |
| 私有 KWin layer-shell Vulkan harness | 最终二进制 22/22 项通过，另含注入呈现重试；validation 0 errors / 0 warnings |
| Qt 原生 Wayland GPU | 共享面板、配置读写、布局恢复、Vulkan → GL → Vulkan 重建通过；三次 Vulkan 生命周期 validation 0 errors |
| 实际 Plasma 6 KPackage | 一轮创建/销毁及一次配置重建通过，重新打开磁盘文件确认 UUID，renderer 创建/销毁计数平衡 |
| 原生 GL / GLES 壁纸 ABI | 各 2 个用例、40 个断言通过，检查初次不写、忽略资产目录旧配置和写入失败诊断 |
| 格式及差异 | 44 个改动 C/C++ 文件通过 clang-format 检查；`git diff --check` 通过 |

配置专门回归涵盖：XDG 无写入默认值、相对路径归一化、CLI 覆盖、复制配置后的实例隔离、
非法 schema、有效 ABI 句柄上的失败、保存失败保留旧文件、并发原子替换、符号链接别名、
背景保存时机、实际 UI 位置锁定操作，以及迁移拒绝覆盖。player 的五项子进程检查关闭
显示环境，验证显式保存无需窗口；迁移工具另有三项回归。

独立壳的配置生命周期检查先显式保存启动文件，然后仅用 `--config` 启动普通运行，
不使用 `--no-save`。分别检查没有面板文件及已有格式化面板文件的情况：等待初始化和
背景加载、正常退出后，启动文件字节不变，前者仍不创建面板，后者字节不变。
输入验证仅通过项目接口和私有宿主，没有向用户系统窗口注入输入。

本地证据在 `build/evidence/configuration-20261021/`：

- `cpu-tests-final.log`、`qt5-headless-final.log`、`qt6-headless-final.log` 和构建日志；
- `x11/results.json`、`layer-gl/results.json`、`layer-vk/results.json` 及各自日志/客户端截图；
- `final-qt-gpu/` 的包探针、GL/GLES ABI、Vulkan 重建日志与图像；
- `metadata.json` 保存基线和最终二进制 SHA-256，各壳结果另外冻结实际执行时的哈希。

X11 和 Qt GPU 证据取得于最后补充位置锁定偏好之前；该补充通过最终 CPU 的实际 UI
操作与重载回归及 Qt 重编译/无显示检查，未重复整套 GPU 检查。layer-shell 两后端证据
来自最终二进制。最终二进制哈希与较早 GPU 证据哈希分开记录，不将它们视为同一次构建。

## 性能与剩余范围

新增路径解析和 JSON I/O 在启动或显式用户保存边界。没有为配置增加逐帧文件访问或 GPU
资源；用户保存新增 `fsync`，未测量慢存储上的交互延迟，不宣称 CPU、内存或功耗改善。

没有安装或重配真实桌面，没有执行外部资产修改、远端 Actions、ARM64 或发布。
真实 Plasma 5 磁盘持久化、物理多屏/多桌面、原生 Xorg、Sway/Hyprland/niri、其他驱动、
热插拔、长期运行和性能仍待分别验收。layer-shell 的运行中 scale 检查未执行，结果中
标记为未覆盖。私有 KWin 的合成图像导入警告保留在日志；上述图像比较是客户端输出，
不代表最终桌面合成像素通过，也不代表全部真实模型的动态或交互验收。

执行：Codex，GPT-6 家族（会话未提供精确变体）。阶段为 CFG1；改动未对外发布。
