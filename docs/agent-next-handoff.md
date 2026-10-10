# 当前独立任务交接

当前HEAD `39762b6`，已有用户架构草稿，本轮只完善文档。用户最新优先级：先让GLM下沉player的UI与控制切换，为后续Vulkan保持解耦。详细范围/所有权/负例/视觉和性能门槛见[专项交接](wallpaper-application-handoff.md)。接手声明实际base与文件，保护未提交工作；GPU用`build/gpu-validation.lock`，不注入系统输入，原素材只读。每阶段验证后commit，标真实Agent/Model/Context。

| 顺序 | 负责人 | 任务与退出条件 |
| --- | --- | --- |
| G31A 先做 | GLM | Controller/资产目录/配置/CPU加载及角色背景切换状态下沉，**同步迁移player并删除旧业务实现**；应用不依赖UI、图形API或窗口，GPU/音频服务边界明确 |
| G31B 随后 | GLM | UI布局/命中/事件/绘制数据下沉到ui，GL执行留render_gl，**同步迁移player并删除旧UI实现**；CPU UI可无窗口测试，受控图像/输入/DPI/释放验证通过 |
| 阶段抽核 | Codex / Gemini | Codex审模块、异步/线程/资源与性能证据；Gemini看阶段前后固定UI/模型/切换图。未看图/未跑不能计PASS |
| G32 G31后 | GLM | 扩当前ABI和薄C宿主，迁移Plasma并删重复扫描/QML控制面板/配置切换业务；宿主只经ABI，声音/设备不复制 |
| G30 后续 | GLM | 在共享应用与服务中完善正式声音/背景、UI/渐变/reset余项，不继续在桌面壳补业务 |

application与ui都保持后端无关；OpenGL/Vulkan对象、shader、GPU字体/图标上传属于对应后端。当前只实现GL，未来Vulkan复用数据/资源契约；不建立空render_vk或万能后端工厂。player在G31使用共享C++接口验证，桌面适配器在G32只经C ABI。

## 既有提交与仍待验收

G27部署、G28触摸、G29动态/分配/195矩阵和V19视觉已有后续提交与报告；本轮文档工作没有复核这些报告全部证据，不能重标为独立验收PASS，也不能按旧“尚未提交”状态要求盲重跑。入口：[G27](g27-plasma-deploy-report.md)、[G28](g28-touch-report.md)、[G29](g29-validation-report.md)、[V19](v19-touch-visual.md)。仅覆盖的条件有效，locator/ASan/全动态等缺口按实际证据逐项复核。

C8由Codex继续诊断3个bust_LR20及真实嵌套/2标量反例，未确认原因不改正式公式。候选触摸数据保持不应用。V20实际桌面/部署视觉在对应构建和部署复核后安排；安装、用户配置和shell操作按用户实际授权，不由本交接自动授权。

共享层迁移前不继续扩Plasma业务面板；真正桌面、长期8–24h、多屏与发布仍是独立退出条件，G31完成不代表最终引擎或Vulkan交付。
