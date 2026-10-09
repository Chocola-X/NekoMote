# G17 / G16 / G18 / V12 独立复核

基线2b4e507，执行Codex / GPT-6（精确变体未暴露）。用户已安装plasma-sdk；plasmoidviewer/plasmawindowed/kpackagetool6及KDE CMake开发包实际可用。

| 范围 | 复核结果与修复 |
| --- | --- |
| G17 | 15失败复现可信，但静态工具错误解包真实schema6的frame.content，跳过22999个active帧。已修：15资产696 parts/6861层/30139帧，156继承+39zx+39zy，共234静态候选。不能宣称“唯一触发条件”；zx/zy多为未绘制标记。新账本保留195分母：15实际FAIL、180 NOT_RERUN，来源冻结无变更 |
| G16 | 原交付仍STATIC模块/Item wrapper、无配置schema，非GL在show前返回，max-steps冻结动画。已补动态插件、真正WallpaperItem/config绑定/schema、配置变更重建、有效暂停和诊断隔离、异步截图/寿命/销毁检查 |
| G18 | 原16个函数不受unittest执行，缺sample/region/错反应可假PASS，17个idle点触摸前仅119步。已修28个合成unittest、冻结资源/完整样本/镜像/优先级与具体反应校验，新目录保留raw请求响应；39/39实测通过，idle事件121 |
| V12 | 39点确有原始请求响应，但来源参考错误反转Y且复制导入区域，X250候选空白点未执行，2160高图截于1600不能称模型结束。删除硬编码观察生成器及7固定数量测试；撤回1MATCH/2MISMATCH认证，3 PENDING、177 UNREVIEWED、15 BLOCKED；两个候选仍不应用 |

## 实际Plasma隔离加载

`nekomote_plasma_package_probe MODEL SESSION NEW_OUTPUT_DIRECTORY` 使用真正的KPackage、Plasma Corona/Containment、WallpaperItem及动态QML导入；使用仓库内临时XDG配置/数据/缓存，未改正在运行的桌面、安装用户插件或重启shell。
Chocola/Fraise实际包显示角色并目视检查；配置页通过KPackage路径加载实例、schema/defaults生效；坏模型到达实际包加载分支并公开Failed，宿主正常退出。它是隔离的实际Plasma库宿主，未认证真实桌面背景、图标/多屏、长期运行或正式声音/背景。
实际包截图是常态播放的诊断图，不作为确定性像素PASS。独立QML组件的三个一致初态（Chocola、Fraise、DPI1.5）与原PSB/WebGL alpha/黑白合成RGB最大差0，门槛不放宽；本轮实际查看并排图。

## 验证与证据

Release/WERROR动态模块与三个宿主构建通过，正式源使用项目warnings，Qt注册生成文件单独例外。Python239执行：229通过/10Skipped；G17合成31、G18合成28包含在其中。Qt自动入口12通过。
四安全负例及真实常态advance、pause/resume、hidden、failed/输入隔离/销毁平衡通过；单次30步固定图形释放重建通过；software实际窗口初始化后status3、诊断公开、零原生renderer。
常态30次提交累计推进约0.928秒，创建/销毁1/1，不当作呈现FPS或性能收益。ASan重新配置确认Qt/Plasma/preview/player OFF，未本轮重跑完整核心145/ASan143或性能基准。
证据：`build/evidence/integration-review-2026-10-09/`（日志、源码/二进制hash、真实包PNG、qml-verified/results.json）；新G17/G18包分别在 `g17-independent-review-2026-10-09`、`g18-reviewed-2026-10-09`。
未成功的“显示前获取真实WallpaperItem并冻结”尝试保留actual-package-fixed目录/日志；未扩大图像门槛或误称该尝试通过。

后续开发大头交GLM G19/G20/G21，Gemini V13/V14；Codex集中审查和准确语义整合，见[交接](agent-next-handoff.md)。
