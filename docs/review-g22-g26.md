# G22/G24/G26 与 V15/V18 独立复核

2026-10-10，基线 `3bb5023`；复核提交 `7d3abc3`、`1e3af83`、`f7f94de`、`3bb5023`。历史 raw 保留，以下结论替代各报告中过宽的完成声明。

| 范围 | 可保留的证据 | 未通过或未执行 |
| --- | --- | --- |
| G22 | 隔离实际 KPackage 的恢复、帧推进、暂停/恢复、resize/DPI、最终 renderer 销毁；自动测试入口存在 | 安装脚本仅复制 KPackage，遗漏 QML/native 模块且忽略 XDG_DATA_HOME；stage 插件 RUNPATH 指向 build，验证用预链接/build probe 和 LD_LIBRARY_PATH，不能证明干净部署。部分恢复检查引用错误旧 state，批量重建计数不覆盖中间实例，RSS 算法无效 |
| G24 | 三型号来源/session 字段匹配；42 CPU 点输入输出绑定 | 覆盖图 Y 换算少半高540px；固定步在首帧后配置，旧 default/idle120 PNG 相同，120步未执行。下身条件也不证明全部区域入画 |
| G26 静态 | 42/45 几何/图像对照通过，hash有效 | 三个 bust_LR20 保留 FAIL：参考图/trace 等于参考 default，native 参数确实改变几何；不能按参数名改成物理未支持，更不能宣布“无公式反例” |
| G26 动态 | 六动作终态图/几何及三中途事件终态几何；九组物理22通道逐步对照 | 60步与120步前缀相同只证明确定性；没有实际 pause/resume。locator 仅 native 移动，无参照；缺连续图/过渡与部分输出冻结 |
| G26 性能 | runtime CLI 的 CPU 时间采样可作局部证据 | 无 malloc/new 计数，无 ASan 执行证据；renderer/application 数字是含加载截图的进程耗时，RSS 跨子进程累计，不能认证稳态或无泄漏 |
| V15 | 发现旧覆盖图错误的目视观察 | version3 观察未满足既有 ledger 输入/hash/条件契约；实际仍3 PENDING/177 UNREVIEWED/15历史 BLOCKED。错误属证据工具，不能宣布模型区域 MISMATCH |
| V18 | manifest绑定21组观察（6动作终态、15组合）、75文件hash有效；18组对应数值通过 | 三 bust 仍失败；未绑定所称12型号/3 idle120及eye_LR/UD。不能认证这些范围或完整连续动画 |

## 本轮直接修复与验证

固定步/seed 在 renderer 创建前重建 CPU 初态；晚到的固定步请求无副作用拒绝，实时诊断通知通过 synchronize 传递，删除 GUI 对活跃 render state 的写入。通知连接可在初始化后建立。实际包 capture 在 show 前配置，等待首个完成信号并核实 FBO/首delta0/已执行步数。覆盖图中心使用输出窗口中心，两个生成工具一致。G26 静态工具不再按参数名豁免失败，计时工具撤回分配/ASan声明。

Release/WERROR/LTO plasma 构建通过；141核心用例37694断言，真实导入另跑12断言；36触摸工具测试、完整 Python 工具测试、3 ABI入口、Plasma安全/配置及实际包6场景测试通过（含recovery/resize/DPI1.5）。新增安全用例覆盖首帧后启用通知、晚到固定步拒绝及原 session 不变。

Fraise B 重拍五条件 clean/overlay；初态与 WebGL、idle120 与正式 player/WebGL 都容差2零超差，alpha差0；120步日志确认executed=120，首delta0，最终GPU计数平衡。已查看初态参照与校正覆盖图；未据此认证另外两型号/所有区域。没有测量新性能收益，未跑ASan、195动态或真实桌面。

新证据 `build/evidence/codex-review-g22-g26/`（manifest冻结代码、二进制、素材、命令/日志/图及比较）；旧证据不覆盖。安装未执行，桌面/用户配置未修改。G27/G28/G29/V19后续已有提交，本页不再分派这些旧任务。当前执行G31应用层迁移，见[交接](agent-next-handoff.md)；后续交付报告与独立验收分开。
