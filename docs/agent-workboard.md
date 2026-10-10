# Agent 协作任务

实现基线 `39762b6`；用户要求先收拢壁纸应用层，详见[专项交接](wallpaper-application-handoff.md)、[任务交接](agent-next-handoff.md)、[路线图](../ROADMAP.md)。以下区分待实现、已提交报告与独立验收。

| 范围 | 当前状态 | 下一步 |
| --- | --- | --- |
| G31A Controller/player | 待实现；目录/配置/异步切换/渐变仍在player | GLM优先抽取并同阶段迁移player；Codex边界复核 |
| G31B UI/player | 待实现；UI业务与GL调用混在apps/player/ui.cpp | GLM随后拆后端无关UI和render_gl的GPU执行，同阶段迁移player；Gemini固定图/隔离观察 |
| G32 ABI/Plasma | G31后实施；Plasma有重复扫描/QML面板与背景资源跨线程问题 | GLM扩共享ABI/薄C宿主再迁移Plasma，删除重复业务 |
| G30正式服务 | 声音背景/UI/渐变/reset余项未最终验收 | 在共享层完善；禁止继续在壳补业务 |
| G27/G28/G29/V19 | 后续提交及报告已在仓库，本轮未复核全量证据 | Codex按实际范围复核；不把交付声明当最终PASS，不盲重跑有效证据 |
| 单边界0x7fc / C8 | 单边界正式支持、195初态及既有静态/代表idle120沿用；3 bust_LR20 FAIL待诊断，嵌套/2标量不扩大放行 | Codex语义反例；不阻塞独立G31 |
| V20/长期/全动态 | 真实桌面、多屏、8–24h及195完整动态未最终验收 | 共享迁移与实际部署就绪后逐项执行；各自报告 |

GLM承担实现大头；每阶段边迁移player边验证并commit，避免只建空模块。application/ui不能依赖OpenGL/Vulkan，UI绘制数据与GPU执行分离；当前只实现GL，不新增空Vulkan后端。资源ID按设备解析，CPU求值/UI和GPU/音频所有权明确。

声明base/改动文件，保留他人工作；GPU使用`build/gpu-validation.lock`，输入只经应用回调，外部素材只读。报告≤一页，raw新目录冻结工具/输入/二进制/hash/argv/日志/截图。完成/失败/未跑/候选分开。触摸候选不应用；旧历史15 BLOCKED不代表当前CPU加载失败。真实用户安装和桌面配置按实际授权及对应部署验收执行。
