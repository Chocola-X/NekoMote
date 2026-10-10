# 当前整合与接手摘要

2026-10-10，HEAD `39762b6`。本轮为用户架构草稿完善与协作计划调整，没有实现代码或重跑引擎验收。以[规范](../AGENTS.md)、[专项交接](wallpaper-application-handoff.md)、[清单](agent-workboard.md)和[任务交接](agent-next-handoff.md)为准。

当前偏离：application主要是单模型Session；资产目录/配置、异步角色切换/渐变和GL UI仍在player，Plasma又新增目录扫描、切换和QML控制面板。下阶段先GLM G31A共享Controller并迁移player，随后G31B共享UI与GL执行拆分并迁移player；G32再扩ABI、升级薄C宿主并收拢Plasma。没有要求重写现有求值核心。

后端边界：application和ui都不依赖图形API；ui生成后端无关布局/绘制数据，render_gl执行GPU绘制。未来Vulkan复用同一业务/UI及资源数据契约，设备/native输出/同步由后端实现；当前不创建空后端或复杂插件工厂。模型/角色渐变状态只归Controller，壳不维护另一套状态机。

既有G22/G24/G26/V15/V18独立复核见[记录](review-g22-g26.md)。其后G27/G28/G29/V19与player异步切换修复、Plasma扩展已提交；交付报告存在但本轮未复核全部证据，不沿用过时“尚未开发”说法，也不宣布已最终验收。单边界0x7fc/195初态沿用；3 bust_LR20 FAIL、嵌套/2标量、触摸候选、195完整动态、正式桌面/声音背景/长期/多屏仍需各自证据。

Codex集中边界/线程/语义与抽核，GLM实现主体，Gemini固定画面/UI输入隔离观察。真实用户安装和桌面配置只在对应构建/部署复核与实际授权下执行；G31交付不代表Plasma或Vulkan已经完成。
