# G16交付与独立整合

原交付3ee1cb9（CodeArts / GLM-5.2），Codex 2026-10-09复核修补，当前见[独立复核](agent-review-2026-10-09.md)。

原实现内部定时器/后端检查可保留，但未完成动态模块、真正WallpaperItem/config/schema；普通max-steps仍冻结。独立整合已补：

- SHARED QML模块/动态插件、build内KPackage壁纸布局；主QML为WallpaperItem，绑定配置，显式配置变化重建实例。配置UI移到正式ui/config.qml，schema在config/main.xml。
- 组件统一status/paused/visibility/window exposure后的有效暂停；失败/隐藏停调度并拒绝输入，恢复重置revision与墙钟。真实SG初始化后报告非OpenGL诊断。
- 常态与固定诊断分离、禁止真实指针污染固定图；异步截图/save检查、专属回调context及窗口销毁后检查GPU计数。
- 新SDK宿主nekomote_plasma_package_probe：真正KPackage+Containment+WallpaperItem加载，临时XDG配置仅在workspace。Chocola/Fraise显示、配置页实例/schema、实际坏资产错误通过。

普通Qt组件三个一致初态对WebGL差0；实际Plasma包常态截图另作目视证据，不混称像素认证。正常时间推进、暂停恢复、隐藏/失败、software窗口与单次图形重建通过；完整交互/多次配置变更/多屏/卸载长跑交GLM G19。

SDK可用，未安装正式用户包或改正在运行桌面。声音/背景服务、桌面图标及正式安装仍未验收；不声明性能收益。
