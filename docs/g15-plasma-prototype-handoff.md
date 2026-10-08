# G15：独立实现最小Plasma/QML原型

此为历史交接契约；当前G15复核与待修项见[独立复核](agent-review-2026-10-08.md)，新实现领G16，不重复领G15。

执行CodeArts / GLM-5.2；本轮C7交付提交为基线，先声明实际HEAD/修改文件。
可以独立处理此复杂任务；接口、生命周期与验收已明确，不需要等Codex逐段编写代码。
先读 [Qt宿主](qt-quick-host.md)、[ABI契约](abi-contract.md)、[协作清单](agent-workboard.md)。
目标是可构建、可隔离加载的QML原生模型模块及Plasma壁纸包；不是正式桌面安装验收。

## 两阶段实现

A. 第二个Qt调用者建立时，把现有`apps/qt_quick_host/session_item.*`移为
`src/host_qt_quick/`私有Qt共享桥；独立宿主与QML模块共用它，不复制renderer/CPU状态/输入换算。
保留既有C ABI和已验证行为，不加兼容wrapper。核心不依赖Qt，桥仅依赖C ABI与Qt。
迁移CMake/调用者，普通Qt诊断行为和截图通过后提交G15-A。

B. 实现`adapters/plasma/`中的QML原生组件、build内KPackage壁纸包与隔离QML验证宿主。
QML URI统一`NekoMote.Wallpaper`，包ID `org.nekomote.wallpaper`。
组件最小属性：modelFile/sessionFile（本地QUrl）、originX/Y、scale、paused、status、errorString。
第一原型只支持组件创建时加载模型/会话；后续改源明确拒绝并报告，不暗中重建或返回成功。
输入/暂停交给共享桥；不复制反应、物理、纹理管线、声音或背景业务服务。
完成加载/能力/错误和包验证后提交G15-B。诊断宿主可以静音，正式声音/背景留待整合。

## 必须守住的边界

- 构建选项默认OFF，Qt 6.9.1依赖保持可选；ABI/纯CPU/ASan仍可独立构建。
  QML动态模块需要PIC时按实际静态依赖目标设定，不猜target名称或全局泄漏Qt include。
- Qt桥是实际两个调用者共享的宿主叶模块。核心/public C ABI不能出现Qt类型，不把Qt桥
  放进runtime或依赖普通player；Qt框架覆写按其API，其余C++遵循项目命名。
- 壁纸QML使用WallpaperItem，不修改全局RHI。模块检测宿主实际backend，仅接受OpenGL，
  非OpenGL显式UnsupportedBackend/error；独立probe进程可显式选OpenGL。
- CPU加载/解码在加载边界，之后求值只在图形线程。GUI通过同步/有界输入转交，不读写CPU实例。
  CPU状态长于所有renderer；GPU由Qt图形线程销毁，换源/卸载不留下悬空借用。
- 加载/绘制失败显示可读来源错误，不能退出宿主或写READY/空图掩盖失败。
  status区分未配置/已加载/失败/不支持；CPU已加载不代表画面验收。
- 坐标逻辑像素→FBO物理像素→中心输出，Y向下，不再镜像。沿用schema6绘制镜像。
  每帧不加载PNG、不上传图集、不GPU→CPU→Qt；截图只在显式诊断。
- 不把GLFW窗口嵌进Plasma，不复制已有Qt桥为第二实现，不改GPU/求值公式、ABI签名或外部素材。
- Qt回调失败和部分初始化保持RAII，异常不越过插件回调；报告尚未注入的Qt框架极端失败范围。

## 验收与交付

提供Qt组件的最小独立QML场景与probe；参数给本地model/session、尺寸/DPI与新截图输出。
素材不放包中，不硬编码本机资产/外部仓库路径。确定性截图可用显式冻结/暂停初态，
不得把测试诊断参数放在壁纸用户配置中。使用公开grabToImage核对Qt内容，记录其离屏范围。

1. Release/WERROR构建；ABI+Qt、无preview/player可构建，ASan默认无Qt/窗口。
2. Chocola/Fraise固定初态、移位/缩放、1.5倍DPI与C7冻结图或独立WebGL对比：
   alpha及黑/白合成RGB≤2字节且超门槛像素0；来源/二进制/输入hash冻结，不能只检查PNG存在。
3. 缺模型、错资产版本、非OpenGL、缺配置可读错误；失败不能退出probe宿主或伪报就绪。
4. 重复创建/删除QML组件、暂停/释放图形资源/重建，状态与资源生命周期符合C7契约。
   实际未完成的测试明确FAIL/Skipped，不宣称上下文驱动失效或多屏/长期已经通过。
5. build中准备完整包（metadata、QML、模块/安装布局说明），只读验证元数据/加载方式。
   KDE SDK缺失时记录缺失及已有Qt组件证据，不安装系统依赖、不改桌面配置、不重启plasmashell。

保留新目录原始命令/stdout/stderr/退出码与hash，GPU共享flock串行。以
`docs/g15-plasma-prototype-report.md`≤一页说明源码/包/测试、真实通过范围及未完成项，
提交填写实际Agent/Model/Context=G15-A/B。Codex复核架构/线程及高风险负例，
Gemini V11核对实际Qt画面；通过后再规划真实Plasma桌面加载与已有应用服务整合。
