# G13 / G14 / G15 / V10 独立复核

基线 `fed812e`，复核 Codex / GPT-6（精确变体未暴露）。当前状态以本记录和协作清单为准。

| 交付 | 独立结论 |
| --- | --- |
| G13 | 接受为全目录诊断证据：195条，180 OK/15已知求值FAIL；720条件、7200区域点、1440截图。复核40094个路径/hash记录、8700个原始result，缺失/错hash为0。不认证全目录视觉或可见触摸 |
| G14 | 接受自动入口，修复默认discover误启动现存Qt二进制、GPU锁及CLI负例。显式Qt入口12项通过；默认Python180项中170通过、10跳过。尺寸断言仍不能替代像素对照 |
| G15-A | 共享Qt桥可保留，本轮三个QML初态与冻结真实WebGL对照通过 |
| G15-B | 未验收为Plasma壁纸：静态插件未提供宿主动态导入；包缺WallpaperItem/config绑定与schema；组件无独立播放调度、真实非GL状态未实现。交G16补齐 |
| V10 | 撤回18 MATCH/2 MISMATCH及候选READY实测结论。逐姿态证据未绑定、候选边缘/近空白没有请求响应、下身可见性描述错误。重分类20 PENDING_EVIDENCE、160 UNREVIEWED、15 BLOCKED_INPUT；两个候选不应用 |

## 已完成修复与实测

- Qt要求 `createRenderer()` 返回非空；旧壁纸组件缺配置/加载失败返回nullptr，可崩宿主。共享renderer现在支持空状态并清透明画面，加载后的状态只在同步边界获取，未创建模型GPU资源时不计模型renderer生命周期。
- 壁纸组件把渲染/输入失败信号映射到公开错误状态；加载后origin/scale变更明确拒绝，非法/非有限值拒绝，避免静默无效。
- 新 `plasma_item_safety` 在正尺寸OpenGL窗口验证未配置、缺模型、坏资产、非法缩放四种情况：透明内容、宿主存活、状态正确。不是在show前退出的负例。
- 删除硬编码观察/候选点的V10生成器及读取预制账本固定计数的7个测试。账本version3要求每条件原图/覆盖图/探针绑定G13实际路径/hash；新增缺姿态/错姿态/未知姿态等合成负例。未建立真实逐点验证器前，候选保持PENDING。
- Qt/Plasma Release/WERROR构建通过。独立 `find_package(KF6Package/Plasma/PlasmaQuick CONFIG REQUIRED)` 配置成功，SDK存在；旧pkg-config探测不能支持“SDK缺失”结论。

证据根：`build/evidence/agent-review-2026-10-08/`。
`qml-verified/results.json`保存二进制/hash/命令及三个QML对原PSB/WebGL截图的比较；Chocola初态、Fraise初态、Fraise DPI1.5的alpha及黑白背景合成RGB最大差均为0（冻结门槛2）。接手者可从C7参考JSON重现相同初态。本轮实际查看并排图 `qml-verified/visual.png`，保留镜像和遮罩姿态，不改正式镜像。
先前Fraise采样误用originX=80对比参考originX=0，作废产物保存在 `qml-pixels/`；最终逐条读取参考JSON参数，不扩大门槛。
截图属于诊断GPU→CPU读回，不进入正式播放和性能测量。没有测全应用零分配、功耗或长时稳定性。

## 当前不可选型号

`coconut-hawaii-a`属于已知15项CPU拒绝。`UnsupportedTransform`是几何语义拒绝，不是声音失败；首个节点不等于全部缺口。
历史 `030512d/a814cbd`曾近似接受部分继承，`a858f01`恢复拒绝：当时Coconut对照仍有8.876px几何误差、13111个超差像素，见 `review-2026-10-07.md`。
不能用旧版“能加载”认证画面正确，也不能按日文图层名字跳过变形。GLM G17整理当前支持缺口及历史变化，Codex负责准确语义和最终验收。
