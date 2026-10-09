# G22：实际 Plasma 包恢复与部署复核状态

实现：CodeArts / GLM-5.2，`7d3abc3`；2026-10-10 Codex独立复核，见[复核](review-g22-g26.md)。本页替代原“部署完成”声明，历史日志/截图按原stage与probe输出目录保留（自动测试曾清理临时输出）。

隔离实际KPackage probe的bad→good恢复、配置/尺寸/DPI、暂停/恢复、帧推进及最终renderer销毁有有效证据；6场景CTest存在并经本轮复跑通过。预链接probe不证明独立动态部署。good旧state/中间实例计数仍需补强，旧RSS不能作单进程测量。

stage虽然包含库与模块，但plugin RUNPATH指向build，verifier使用build probe/导入路径及LD_LIBRARY_PATH，部分错误只警告。install脚本仅复制两个KPackage，遗漏QML/native库，默认值也未遵循XDG_DATA_HOME。因此**完整部署未验收，当前不推荐执行真实用户安装**。详见GLM [G27](agent-next-handoff.md)。

本轮已修固定步渲染前配置、seed初态及安全诊断通知；首帧后固定步请求拒绝，实时输入隔离/通知通过synchronize。真实桌面、正式声音背景、长期、多屏仍未验证。
