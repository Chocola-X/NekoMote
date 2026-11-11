# Plasma 5 首次配置后停在 Loading 的核查

基线：`a471d1b`，2026-11-11。用户反馈：Manjaro KDE Live
`23.1.4-240406-linux66`、Plasma5.27、默认OpenGL；首次选择壁纸并填写资产目录后
黑屏、红色Loading，切换桌面布局后恢复。另一KDE6 Live没有复现。

## 已确认的代码缺口

Plasma5包在 `Component.onCompleted` 中仅生成一次 `panelInstanceId`。
播放器的 `assetsDir` 绑定以非空实例ID为门控，避免读取共享默认面板配置。

当QML对象存活期间，配置桥换入一个没有ID的新map，或重载将ID清空时，
初始化处理不会再次执行。此时包根节点已经有资产目录，但native实例ID和资产目录为空，
status为0，正好显示红色Loading；重新创建包对象会重新生成ID而恢复。
这属于插件包的初始化缺口，不应归类为Plasma不可修复缺陷。

**尚未证明该Live镜像在用户具体操作中必然替换/重置了配置map。** 本轮没有该镜像
运行日志，也没有重建完整Live桌面。本轮确认的是能够产生同样现象、且旧测试遗漏的
配置生命周期问题。不能仅凭状态一致宣称排除了全部其他原因。

## 修复

- Plasma5入口观察当前配置ID，在组件完成后、ID为空时补齐并保存。
- 配置变化的处理经 `Qt.callLater(ensureInstanceId)` 延迟到绑定更新之后。
  同步在ID变化回调里写回map的候选试验仍使native绑定停留为空，已撤回。
- 回调始终读取当前map；已有非空ID保持不变，不覆盖持久配置。
- 保留未获ID之前的资产门控，不加载共享默认面板、不引入周期性重试timer。
- 不修改模型、渲染器或Plasma6入口。新增工作仅发生于配置变化，不增加稳态帧工作。

## 验证与边界

1. 扩展 `tests/plasma/test_plasma5_package.cpp`：加载真实staged模块与包入口，
   同一QML对象内将wallpaper.configuration替换为有资产目录、无ID的新map。
   原入口失败：`id= assets= status=0`；修复后生成有效UUID，native获得目录，status=1。
2. 后续资产目录编辑不会再次写ID；切回原map保留原ID；再创建包对象保留已有ID。
   既有containment/main/config导入检查一并通过。
3. 另用实际 `KConfigLoader` + `KConfigPropertyMap`、真实包schema做本地诊断：
   显式清空map里的ID后，原入口仍为status0，修复后native目录恢复、status1。
   此诊断证明真实KConfig属性映射下修复也生效，不等于完整Plasma应用配置操作复现。
4. 独立执行者只读复核配置回调、对象销毁与UUID稳定性，无阻塞项。

环境：本机Qt5.15.17；使用既有staged Qt5插件的隔离副本，只有包QML替换成候选，
并以Qt5 Quick/Qml、`-Wall -Wextra -Werror`重新编译最新包测试源码。
未修改用户安装、桌面或外部资产。测试采用offscreen/software，不创建GPU renderer；
**status1表示加载门控解除，不表示真实模型已绘制**。

本机完整 `build/plasma5` 重新配置受缺少 `Qt5LinguistToolsConfig.cmake` 阻挡；
没有把运行旧测试可执行文件误算为新增回归通过。改为单独CMake/Automoc编译上述
同一测试源，运行相同stage/import参数。此次没有重新构建原生库，也没有执行
Ubuntu20/Qt5.12、远端Actions、GPU或目标Live镜像验收。

证据：[原入口失败](validation/plasma5-first-use-2026-11-11/p5-first-negative.txt)、
[修复通过](validation/plasma5-first-use-2026-11-11/p5-first-positive-final.txt)、
[真实属性映射修复前](validation/plasma5-first-use-2026-11-11/p5-realmap-before.txt)、
[修复后](validation/plasma5-first-use-2026-11-11/p5-realmap-after.txt)。

待目标机器用新包验证首次安装→选择→填写资产路径→应用，确保无需切换布局即可播放。
如仍停在Loading，下一步需采集该会话的QML警告、实例ID/资产目录/status变化，
再区分配置未就绪与渲染调度问题，不把本次候选修复写成完整实机认证。

## 后续：Plasma6 后端应用后的同类缺口（2026-11-11）

用户在 Manjaro24 / KDE6 Live（内核6.9）遇到资产首配正常、切 Vulkan 后红色Loading，
布局重建可恢复。Plasma6入口此前仍只有一次性UUID初始化，现已补运行中 map 替换/ID
重置处理。与先前Qt5修复相比，本次还在同一QML对象内保留最近的非空UUID，临时空ID
恢复原身份，已有显式ID优先；不会因为改后端而切换到另一份面板配置。

真实QML/native配置链回归复现原代码 status0 并验证修复，包含显式新ID、多次后端
选择和销毁重建；新增必选CI runner，Qt6.4、Qt6.9和最终包入口通过。配置就绪与实际
GPU绘制分开记录，未声称已经重现Live宿主的完整操作或排除其他Vulkan重建问题。
详见[本轮证据和边界](validation/plasma6-backend-apply-2026-11-11/README.md)。
