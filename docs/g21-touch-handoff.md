# G21：触摸CPU证据

CodeArts / GLM-5.2，base32a57a1；Codex复核82ab3b6。42个最终result均OK，逐点request/response hash有效；两个Cinnamon head点已真实重跑为h08，旧summary.json仍保留28OK/2FAIL，不能直接用旧汇总。独立复核索引在 `build/evidence/review-g19-g21-2026-10-09/g21-results-reviewed.json`，原失败不覆盖。

42点：moved15、scaled_half15、default边缘12；加G18的39点，共81个CPU点。idle120事件121，中心投影/Y向下/镜像保持；Fraise胸中心输出357.5→1080高PNG897.5。它们证明现有区域/具体反应行为，不证明区域贴合图像，也不验证候选。

来源文件实际为 `build/evidence/g21-touch-source-field/source_field_reference.json`，旧报告路径错误。直接原模型pixelMarker、presentation cutoffY/touchMarkers重算与session五区域数值一致；该文件尚缺来源路径/hash及可重复生成入口，不能充当完整冻结参考。

测点spec及raw在 `build/evidence/g21-touch/source-specs/` 和 `validated/`。没有新的Qt/FBO下身布局PNG；旧2160高GLFW图实际截到1600，仍不能判断素材网格下界。

GLM G24补来源manifest/生成入口、准确汇总、新Qt/FBO条件图与逐点图像绑定；Gemini V15消费实际图像填写真实version3观察。现有3 PENDING/177 UNREVIEWED/15 BLOCKED及候选不应用保持。
