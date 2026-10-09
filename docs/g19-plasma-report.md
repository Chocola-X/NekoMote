# G19：Plasma 配置与生命周期

基线1eea941，原实现CodeArts / GLM-5.2；82ab3b6后由Codex独立复核。配置页的FileDialog、URL提示和范围限制保留；实际KPackage宿主可加载模型。

原“全部完成”撤回：配置页只检查路径而未创建；40轮只看新对象地址/created，未检查旧renderer销毁；错误分支只有失败截图，没有恢复；resize未同步containment；轮询last_delta无法证明恢复首帧。安装示例只有KPackage，缺native库/plugin/qmldir部署，不能用于正式安装。

本轮补修真实QQmlComponent创建、containment resize、QPointer重建身份、每代created/destroyed平衡及窗口销毁后的最终计数；截图失败明确失败。恢复首帧通过诊断frame_ready信号检查delta0。初态QML绑定原先排队重建、丢弃诊断实例，现仅完成初始化后处理配置变更。移除产品QML的计数入口和无必要每帧atomic<double>写入。

入口：`build/plasma/bin/nekomote_plasma_package_probe --model MODEL --session SESSION --output NEW_DIR --lifecycle`。普通截图不加lifecycle；bad-asset/invalid-config目前只证明错误公开与GUI存活，仍不验证恢复。仅workspace隔离XDG，不安装/修改真实桌面。

实际20位置+20缩放重建已重测，每轮旧renderer1/1，最终1/1，恢复首delta0；隐藏先排空已提交帧再检无后续帧。新原始日志/hash/PNG见 `build/evidence/review-g19-g21-2026-10-09/`。配置拒绝默认9项；第10项真实资产输入隔离需显式环境变量，不能算默认10合成PASS。以上为G19历史范围；后续G22恢复/DPI/自动测试已有部分证据，完整安装/独立动态部署尚未验收，见[当前复核](review-g22-g26.md)及GLM G27[交接](agent-next-handoff.md)。不据截图宣布长期稳定。

后续Loader实例目前未重复诊断输入隔离，40轮计数有效但最终图不是受控输入对照；G22需每代在渲染前通过明确诊断接口禁用实际hover/mouse，不改共享渲染状态。
