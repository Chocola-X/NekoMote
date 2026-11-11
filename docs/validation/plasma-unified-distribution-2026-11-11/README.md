# 统一 Plasma 分发的验证记录

基线 `b136df2`，2026-11-11。环境、源文件和最终归档 SHA256 见 environment.json。
本机 Qt5.15.17 / Qt6.9.2 / Plasma6.3.6 / x86_64；本轮仅修改分发、安装与 CI，未改播放帧路径。

- `python3 -m unittest discover -s tests/reference_capture`：180 项，12 项按环境跳过。
  包含 17 项安装事务测试、实际合成 ELF 分发测试及模拟 GitHub 发布门禁；日志里的
  argparse error 来自预期失败的负例，不是本轮失败。
- 安装器覆盖旧文件/环境修改、路径穿越、符号链接、原生库哈希、依赖缺失、两方向
  JSON/desktop 身份不一致的拒绝；KPackage 部分写入、重装移除后失败、静默未安装均回滚。
  这些故障注入只在临时目录使用模拟命令，不对用户桌面注入失败。
- 使用最终外层包中的 installer.py 和真实 kpackagetool6，在隔离 XDG 目录完成
  首装、同版本重装、状态及卸载；随后调用基线旧 plasma_deploy.py 的 do_install
  生成真实旧分离布局，再迁移、导入并卸载。旧库/记录/环境文件消失，资产和面板哨兵保留。
- 两个最终包分别在无全局 QML import 路径的新进程中设置页优先导入，再创建 native item。
  Qt5 采用已有 Qt5.12.8 基线库，Qt6 采用本机 Qt6.9.2 库并如实声明；没有冒充完整重编译
  Qt5 插件或 Debian12 / Qt6.4.2 通用归档。
- `bash -n`、Python 编译、Python3.8 语法检查及 diff 空白检查通过。没有实际 Python3.8
  解释器运行记录；源码仅使用 Python3.8 可用标准库。
- 独立 Agent 复核发现 incoming identity fallback，修复后独立 17/17 安装器用例通过，
  未发现剩余阻塞问题。旧安装识别仍可使用 desktop，但新 ZIP 严格要求 JSON 且两者一致。

真实操作只使用项目下 experiments/unified-plasma/{data6,config6,cache6}；没有安装到当前
用户桌面、重启 shell、切换壁纸、推送或发布。原始重现脚本与归档保留在该忽略目录。
CI 新增最终归档的 settings-first / native directory import；远端完整容器、ARM64、真实
Plasma5 桌面、在线 KDE Store/KNewStuff 和 GPU 播放仍未验收。

复现工具测试：

```bash
python3 -m unittest discover -s tests/reference_capture
bash -n tools/ci_build.sh tools/ci_publish_release.sh
```
