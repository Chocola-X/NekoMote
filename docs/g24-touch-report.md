# G24：真正可消费的触摸视觉证据

CodeArts / GLM-5.2，base `7d3abc3`。G18/G21 最终 81 点有效不重复；本轮补来源冻结入口、Qt/FBO 条件图与综合冻结 manifest。候选触摸数据保持 PENDING。

## G24-1：来源冻结

- 新增 `tools/touch_audit/generate_source_reference.py`：直接从原仓库 `pixelMarker`（source.json metadata.charaProfile.pixelMarker）和 `presentation`（config/model-presentation.json cutoffY/touchMarkers）重新计算 source_regions，逐字段比较 session.nks.json 的 interaction.regions。公式与 `import_session.py` 完全一致。
- 三模型 source_regions vs session_regions 全部 MATCH（逐字段 abs_diff ≤ 1e-6）。
- 新增 `tools/touch_audit/bind_point_evidence.py`：42 点 CPU 证据绑定到 source-manifest。42/42 点 request/response/hash 全部验证通过，模型/会话 hash 与来源一致。
- 证据：`build/evidence/g24-touch/source-manifest.json`、`point-binding.json`。

## G24-2：Qt/FBO 条件图

- 扩展 `adapters/plasma/probe/package_main.cpp`：添加 `--origin-x/--origin-y/--scale/--steps/--seed` 参数和 `Qt::WindowTransparentForInput` 标志（防止鼠标交互引入误差）。
- 放宽 `configure_diagnostics` 的 rendered_frames 检查：idle 动画无用户输入时自由推进与固定步长一致，允许在首帧渲染后配置。
- 三模型×五条件（default/idle120/moved/scaled_half/lower_body_in_frame）全部成功。PNG 尺寸=800×1080，alpha bbox 非空。lower_body_in_frame（originY=-200）下身进入画幅。
- overlay 图在 clean 图上叠加触摸区域椭圆，数据来自 source-manifest projected_regions。
- 证据：`build/evidence/g24-touch/qt-captures/`（15 clean PNG）、`overlays/`（15 overlay PNG）。

## G24-3：冻结绑定与契约测试

- 新增 `tools/touch_audit/freeze_g24_evidence.py`：综合冻结 manifest，36 项契约检查全通过。
- 检查项：来源 hash 一致、新条件不假冒 G13、PNG 尺寸/alpha bbox 有效、overlay 存在、42 点绑定、source regions 匹配。
- 新条件 `lower_body_in_frame` 不在 G13 条件集中，独立诊断布局，不覆盖 G13 冻结布局。
- 证据：`build/evidence/g24-touch/freeze-manifest.json`。

## 修改文件

- `adapters/plasma/probe/package_main.cpp`：条件参数 + WindowTransparentForInput
- `adapters/plasma/wallpaper_item.cpp`：放宽 configure_diagnostics rendered_frames 检查
- `tools/touch_audit/generate_source_reference.py`：来源冻结生成入口
- `tools/touch_audit/bind_point_evidence.py`：42 点绑定
- `tools/touch_audit/generate_qt_condition_captures.py`：Qt/FBO 条件图生成
- `tools/touch_audit/generate_overlay_images.py`：overlay 图生成
- `tools/touch_audit/freeze_g24_evidence.py`：综合冻结 manifest

## 未验证

候选触摸数据保持 PENDING。Gemini V15 随后逐图实际观察填写 version3。长期/多屏/真实桌面安装/正式声音未验证。

Agent: CodeArts
Model: GLM-5.2
Context: docs/agent-next-handoff.md G24
