# G15 Qt共享桥/QML交付状态

CodeArts / GLM-5.2：`214fd24`提取共享桥、`9c72813`QML/probe、`c03d2bf`DPI/生命周期补齐。2026-10-08独立复核基线fed812e。

可保留：`src/host_qt_quick`共享Qt桥，默认关闭的plasma preset、C++ QML组件及隔离probe；DPI初始化/componentComplete顺序修补。Codex本轮三个QML初态对冻结真实WebGL图像门槛通过。

**未验收为Plasma壁纸包**：当前STATIC插件仅probe可用，未形成宿主动态导入/部署；包主QML/配置schema与绑定缺失；持续播放依赖probe timer；非OpenGL组件状态与渲染错误未正确公开。原负例在show前退出，遗漏缺配置返回空renderer的宿主崩溃路径。Codex已修空状态透明renderer、失败信号和placement静默无效，并在正尺寸窗口验证四安全负例；其余交GLM G16。

SDK更正：`pkg-config KF6Plasma/KF6Package`不存在不能说明开发包缺失。本机KF6Package、Plasma、PlasmaQuick的CMake CONFIG实际存在，独立find_package三者成功。无需因此停止包开发；本轮不安装/改桌面配置。

grabToImage是诊断GPU→CPU读回，不能写“无读回”；正式播放仍不得常态读回。原单次固定态重建不代替实时暂停/多次组件卸载或实际桌面生命周期验收。

当前证据、边界与下一步见[独立复核](agent-review-2026-10-08.md)、[G16交接](agent-next-handoff.md)。旧运行细节保存在Git报告历史及build产物。
