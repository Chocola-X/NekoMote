# PNG 提交、Ubuntu20 打包与异机资产复核

复核主库 `86da860` 和资产库 `4bced9e`，来源为用户提供的两架构 Actions 日志及
`experiments/NekoMoteAssets/assets/`。结构化结果见
[验证记录](validation/png-ci-assets-audit-2026-10-19.json)。

## Ubuntu20 失败原因和修复

用户提供的 x86_64、aarch64 日志均已完成编译及 210/210 检查，随后在
`cmake --install --component runtime` 失败：找不到 `bin/nekomote_png_recompress`。
新增程序有安装规则，但 `tools/ci_build.sh` 使用显式目标列表，没有构建该程序。
原生包的严格程序集合审计也仍只接受旧的五个程序；只补构建目标会继续在审计处失败。

同步补齐构建和审计中的 PNG 工具，保留其余依赖/符号上限检查；新增真实 ELF
fixture 用例，验证包含六个程序的原生包可通过、缺 PNG 工具拒绝、动态 FLAC 拒绝。
Ubuntu20 和 Arch 文档同步列出六个程序，没有调整发行版基线或放宽测试。

在已有完整 Ubuntu20 OCI 根文件系统中，用 bubblewrap 执行修复后的同一 CI 脚本：
GCC10.5、glibc2.31、CMake3.28.4、Release/LTO/Werror，固定依赖仅从本地缓存取源码，
编译器和系统库均来自镜像。210/210 检查、六程序安装、严格 ELF/`ldd -r` 审计和
归档通过。归档解压后 PNG 工具的 strip/recompress 均在 Ubuntu20 实际运行成功。
主机另跑 210/210 CPU/工具检查通过；两次 Python runner 都包括同一打包回归，不累加。

首次本地隔离复验失败于测试缓存包装器：它给系统依赖缺失负例也创建了 `_deps`。
修正包装器只处理固定依赖模式后完整重跑通过；没有删除或放宽负例，初次日志保留。
这次没有重跑 ARM64 或远端 Actions，不把本地 x86_64 通过扩展成两架构远端验收。

## 资产核对与修复

新树与本地压缩前 `assets/` 按相同相对路径比较：

| 内容 | 结果 |
| --- | --- |
| 324 张 PNG | Pillow 独立 RGBA 对比全部一致；运行时完整解码、裁切像素及 region 对比全部一致 |
| PNG 编码大小 | 983,983,314 → 802,072,704 字节，减少 173.48 MiB；1 张原文件保持原编码 |
| 195 个 `model.nkm` | 对应路径文件 SHA256 全部一致 |
| 会话、263 个 FLAC、型号元数据及其他配置 | 除三份名称字典外对应普通文件全部一致 |
| 202 个相对软链接 | 链接文本一致、目标存在、均在资产树内；所有会话音频引用在本角色内有效 |
| 根级文件差异 | 新树没有个人 `config.json` 和两份迁移报告，符合分发约定 |

三份名称字典确实混入旧内容：

- `characters/characters.json`：195 组四语名称被替换为单个中文字符串。
- `backgrounds/dynamic/background.json`：35 组四语名称被替换成 `BG_xx`。
- `backgrounds/background.json`：200 个语言字段被覆盖，例如 BG_07A 从水族馆广场变成海滨餐厅露台，图片像素却未改变。

三份本地正确字典与资产库上一版 `3c46120` **原字节一致**。已经在新资产目录直接
恢复这三份文件，更新 README 中过时的 `model.nkm.json` 及缺失的 FLAC/链接说明，
并按用户要求 amend 资产库上一提交：`4bced9e` → `faa78f6`。
恢复后 889 个共有非 PNG 普通文件全部相同；压缩 PNG 保留，没有改变实际模型、语音或图片内容。
资产目录当前干净，没有推送，也没有替换主库正在使用的 `assets/`。

## 证据和范围

原始逐文件像素/SHA256、三份错误字典备份、冻结的运行时探针、CTest XML、工具链、
两次隔离日志、归档及审计位于 `build/evidence/png-ci-audit-20261019/`。
原始用户日志仍在 `experiments/evidence-native20-{x86_64,aarch64}/`。
本次只核对压缩、目录、引用与打包，不新增 GPU、真实桌面、多屏或长期运行认证。
