# 2026-11-08 A 阶段修复与验收

任务来源：用户要求修复[独立复核](code-quality-performance-audit-2026-11-08.md)中的问题，
撤回不成立的优化并在测试后提交。基线为 `c86f469` 及接手时另一 Agent 的未提交 A 阶段实现。

## 最终改动

- 保留 geometry 编译结果向 playback 转移所有权，消除重复编译；保留 GL alpha 有限性和范围校验。
- GL/Vulkan 的帧复用同时检查拥有的 producer token 和非零 revision。每个 GeometryInstance
  仅在创建时分配一次 token，renderer 持有 token 防止销毁后的地址复用；成功 build 后递增版本。
  token 缺失或 revision=0 的帧仍完整验证。reserve 和失败的 prepare 均使缓存失效，
  避免驱动报告上传失败后使用旧 token 跳过已被部分改写的 buffer。
- 暂停后稳定画面停止准备、绘制和呈现。X11 / layer-shell 使用 500ms 异步工作轮询，
  输入/resize/恢复可提前结束空闲等待；有更新时仍按原 FPS 调度，不再把暂停 UI 限为 2 FPS。
  Wayland 保留原有 frame callback 和 Vulkan retry/fence 契约。
- UI 动画时间独立于角色暂停。共享 UI 判断包含侧栏开/关动画及图标 hover/press 缓动，
  完全展开和关闭后均可静止。UI 在角色绘制后重置位置引发的 frame 失效保留到下一帧，
  player 与高层 ABI 都会补画。player refresh 和 X11 Expose 也通知重绘。
- **撤回音频失败后释放 PCM bank、切换 Disabled 和新增设备失败降级的初版改动**。
  保留原先的设备重试、当前 bank 与取消静音行为。未承诺后台跳过解码，未引入按需加载、
  LRU 或流式解码。`set_muted/create_audio_device/advance` 的音频行为已与 HEAD 原实现逐函数核对；
  player 的音频实现也保持基线，仅增加暂停绘制相关状态。
- 直接 Over、去除离屏、GLES 非 POT 纹理等建议没有可靠放行证据，本轮不实施；
  原有颜色保护、Multiply、遮罩格式、离屏合成和 padding 行为保留。
  六个工具文件的无关可执行权限变化全部恢复。

配置 schema、C ABI 帧布局、模型求值公式和素材均未改。高层 ABI 只新增串行查询
`nekomote_wallpaper_needs_render`，当前宿主同步重建；其调度边界见 [ABI 契约](abi-contract.md)。
C++ RenderFrame 增加拥有的身份 token，诊断 C ABI 逐字段转换仍生成默认的不受信帧，
不允许外部 C 调用绕过验证。稳态只有 token 引用计数，不在每帧创建 token。

## 验证

环境：Xeon E5-1680 v4，GCC 15.2.1，Release/LTO/Werror，Mesa 25.2.3 / LLVM 20.1.8。
Xvfb 21.1.18、lavapipe 25.2.3 仅解包到 /tmp，专用 :88 显示；音频连接指向不存在的
测试服务、配置位于 /tmp。未安装系统包、桌面插件或改动用户会话。

- 构建 player、ABI、GL/Vulkan、X11、layer-shell、全部适用测试目标成功。
- 最终非 GPU CTest：256 个入口，254 执行通过、2 跳过，无失败。
  跳过的是原 E-mote JSON 导入集成及缺 CJK 字体环境；真实 nkm 资产集成另有实际执行。
- GPU CTest：GL、GLES、Vulkan 及 python ABI 图像共 4 个入口全部通过，GPU 工作串行执行。
  包含既有图像、mask/保护/Multiply、生命周期和已预热路径的分配检查；
  不以这些局部零分配检查推定整个应用/设备线程零分配。
- 新增/增强用例：两个真实 GeometryInstance 在相同版本下共用 GL/Vulkan renderer 的像素回归；
  缺失 producer / 零版本验证；reserve 失效；GL 上传出错后重新准备；
  共享 ABI 暂停打开/关闭面板、静止与 fit；图标/侧栏动画收尾；暂停后 UI 重置引发的待绘制帧。
- 格式检查及 `git diff --check` 通过。

环境准备时，旧 Debian Xvfb 缺 GLX，系统只配置了无设备可用的 NVIDIA ICD，
导致初次 ABI host / Vulkan 检查失败；改用匹配主机版本的隔离 Xvfb 和显式 lavapipe 后通过。
未修改项目检查以绕过这些失败。

## 性能和画面范围

保留的优化收益仍限于：加载时少一次 geometry 编译；重复模型帧少扫描、复制和上传；
稳定暂停少 GPU 提交。token 创建发生在实例构造，新增引用计数是帧查询与准备的固定成本。
局部 prepare 微基准及三个真实模型静态画面与基线 renderer 的比较结果见
[证据及复现](validation/code-quality-fixes-2026-11-08/README.md)。

基线 renderer 比较只替换 GL renderer 翻译单元，几何与共享 ABI 都使用修复版本，
验证的是渲染路径没有改变静态像素，不能称作整套旧版/新版端到端性能比较。
未测真实硬件 GPU、功耗、宿主呈现延迟、真实声音播放、全动态、长期、多屏或真实桌面生命周期。
没有把轮询减少、理论显存容量或软件驱动微秒数外推为整机资源降幅。
