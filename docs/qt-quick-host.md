# Qt Quick 图形宿主试验

本页保存 C7 独立诊断宿主的运行与历史验证范围。实际 Plasma 插件及高层 ABI 已实现；2026-10-11 显示补修/当前安装入口见 [说明](plasma-display-fix.md)。
测试摘要见 [机器记录](validation/c7-qt-quick-results.json)。

## 构建与运行

可选依赖 Qt ≥6.4 的 Quick/OpenGL 公开组件；本机验证 Qt 6.9.1，6.4 基线编译待 CI 执行。
Qt只进入宿主目标；默认构建和无窗口ASan不需要Qt。使用现有C ABI，不新建渲染器或声音服务。

```sh
cmake --preset qt-quick
cmake --build --preset qt-quick --target nekomote_qt_quick_host
build/qt-quick/bin/nekomote_qt_quick_host \
  --model assets/characters/chocola/date-a/model.nkm \
  --session assets/characters/chocola/date-a/session.nks.json
```

正常播放使用约17ms更新请求（约60FPS）。`--run-seconds 12`提供自动退出的连续播放检查；
`--trace-frames 8`仅用于有界诊断，记录本机OpenGL设备、CPU求值/提交时间和Qt呈现事件，
不是GPU完成时间或正式性能基准。声音明确关闭，尚未承载壁纸背景/切换策略。

```sh
# GPU检查串行；输出必须不存在。
QSG_RENDER_LOOP=threaded QT_SCALE_FACTOR=1 flock build/gpu-validation.lock \
  build/qt-quick/bin/nekomote_qt_quick_host \
  --model assets/characters/chocola/date-a/model.nkm \
  --session assets/characters/chocola/date-a/session.nks.json \
  --steps 30 --check-lifecycle build/qt-lifecycle-new
```

`--capture NEW.png --steps N`用`QQuickItem::grabToImage`截取Qt内容场景图的离屏合成；`--qt-marker`叠加黄色Qt图元验证原生状态复位。
`--touch X Y`通过自己的GUI回调队列回放逻辑像素输入，不操作系统鼠标/键盘。
确定性入口不接受真实指针，避免污染初态。`QT_SCALE_FACTOR=1.5`验证物理像素换算。

## 线程、时间和所有权

- GUI创建前完成资产编译和PNG解码；显示后只有Qt图形线程求值会话、上传/绘制。
- GUI把输入和暂停状态交给`synchronize`，不修改渲染线程的会话。输入队列容量32；超限显式失败。
- CPU程序/实例由item与renderer显式共享；程序长于实例。场景图释放后CPU状态保留，GPU对象重建。
- GPU对象在Qt清理renderer时先释放，随后释放CPU借用。窗口销毁等待场景图清理，记录创建/销毁计数。
- 重新接入或暂停状态改变时重置宿主墙钟，不重放隐藏时间；runtime仍执行共有250ms预算。
- 输入为item逻辑像素→实际FBO物理像素→中心输出像素，Y向下、不镜像；session完成作者逆变换。
- 普通帧不读回像素、不解码图片。截图只在显式诊断入口；通知等Qt字符串处理也只在诊断路径。

Qt FBO在OpenGL场景图中绘制，native调用后按Qt要求复位状态。应用自己的`--session`不会传入
Qt的会话管理命令解析器。此进程可选择OpenGL；Plasma 查询宿主实际 RHI；C12 可在 Qt OpenGL 场景图中用 GPU 共享图像承接 Vulkan 引擎，见 [双后端](plasma-dual-backend.md)。不能设置
plasmashell全局后端或把非OpenGL悄悄转换为CPU传图。
依据：[Qt FBO契约](https://doc.qt.io/qt-6/qquickframebufferobject.html)、
[状态复位](https://doc.qt.io/qt-6/qquickopenglutils.html)、
[内容截图](https://doc.qt.io/qt-6/qquickitem.html#grabToImage)。

## 本轮验收边界

受控Chocola/Fraise/Milk普通/动画/移动缩放、1.5倍DPI和Qt图元合成通过。
三个固定初态与原始PSB/WebGL的alpha和黑白背景合成RGB一致；不以裸RGB透明区噪声判失败。
释放场景图→重建，首帧delta为0，尺寸稳定后的图像与初态逐像素一致，GPU创建/销毁相等。
X11隐藏/show/resize的旧尺寸回执会晚到，直接window读回可能得到旧drawable区域；
诊断在`afterFrameEnd`后核对窗口/FBO尺寸，恢复首呈现后重绘，用Qt公开的异步内容截图接口
和独立目标验证合成，不把窗口像素读取当作场景图证据。保留失败尝试；这不是实际Plasma
窗口呈现验收，正常播放不读回。
渲染末尾新增GL错误检查；真实不完整FBO负例证明ABI能返回错误，随后有效目标可恢复。

不认证全195型号、动态Qt宿主音频/背景、真实Plasma、GPU驱动失效恢复、多屏和长期运行。
连续播放曾出现约1FPS的短时异常，原生工作约3ms；默认环境复测3秒91次提交，最终12秒684次提交，根因未确定。
输入也会请求重绘，更新请求不是严格的呈现FPS保证；提交计数不是实际呈现FPS。
不把临时禁用输入法/辅助服务或关闭同步的结果当作正式改进，也不改变桌面设置。
超高GLFW窗口底部裁切与Qt/WebGL不一致另列已知缺口；不降低冻结图像门槛。
最终样本预热3秒、采样6秒，进程CPU平均8.83%（单核100%口径），RSS恒定236,474,368字节；
不包含声音/UI，未测GPU/显存/功耗、帧时间分位或全应用分配，不作性能改善结论。
Qt框架创建renderer/synchronize的极端分配失败尚未完整注入验证；普通矩阵不认证所有异常情况。

动态模块与真正Plasma隔离KPackage/WallpaperItem已接入，当前见独立复核（已归档）。
GLM G19补配置/生命周期/部署，Gemini V13核对实际包；G14入口保持。实际包常态截图与本页确定步组件证据分开。

C17 Wayland 兼容验证见 [说明](wayland-compatibility-2026-10-13.md)。自有宿主默认显式请求 desktop GL 3.3；`--gl-api gles` 可验证 GLES 3.0 上下文。窗口平台由 Qt 自动选择，启动诊断显示实际平台与上下文 API。此选项不改变 Plasma 宿主的全局 RHI。
