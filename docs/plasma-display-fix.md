# Plasma 显示补修与安装测试

2026-10-11，Codex；修复起点 `7029eda`。用户优先要求排查隔离账户中角色可见、UI/背景不可见。本轮不改模型求值公式或外部素材，不修改当前桌面和 miku 的配置。

## 原因与修复

- Qt 在提供输出尺寸前加载实例，UI 初始化条件不成立，后续也不重试。现在先通知尺寸，UI 在首个可绘制帧初始化，失败通过 ABI/宿主明确报告。
- UI 明确绘制到宿主 FBO 并设置 viewport；图标/字体在有效 GL 上下文释放及重建。没有可选图标时显示内置菜单图形。
- 按用户补充要求，角色改为左键短按释放触摸、长按 300ms 后拖动，滚轮围绕指针缩放；右键留给桌面。手势与缩放语义统一在 application Controller，player 和 ABI 均使用它。UI 优先消费输入，离开输出/暂停/reset/角色切换取消未完成手势。
- 壳补齐鼠标按下、移动、释放和滚轮事件，逻辑坐标转换为输出像素后经 ABI 转发；共享层先消费 UI 事件，避免触摸穿透。
- 未选背景的配置仍显示底色。先前重装脚本会删除用户回退配置，现在只卸载记录中的插件文件，保留背景等偏好；先校验新包再卸载旧包。未读取到 miku 的实际配置，不能断言其背景缺失必由配置导致。
- KPackage 测试的缩放判断曾早于场景几何更新；测试宿主随 scene root 同步尺寸，并等待新尺寸及匹配 FBO，保留原尺寸断言。

## 已核实范围

[显示阶段冻结摘要](validation/plasma-display-results.json) 对应提交 `cd911cd`，原始证据：`build/evidence/plasma-ui-background-20261011/`。后续左键手势与当前 0.4.0 安装包见 [C10 冻结摘要](validation/plasma-left-hold-results.json)，原始证据：`build/evidence/plasma-left-hold-20261011/accepted/`。构建为 dev/Debug/WERROR，不是发布态性能基准。

- 当前 Controller/UI/ABI 相关 30 项通过；GPU 渲染 12 项/221 断言通过，包含显式 FBO、文字、晚到尺寸、两次字体/UI 资源初始化及损坏图标错误/修复后重试。旧显示阶段的 11 项/218 断言保留为历史证据。
- Qt 自身输入回调驱动的截图检查：背景上下方向、图标、开关面板、展开设置、选择背景、滚轮、resize、图标拖动；DPI 1 与 1.5、缺可选图标分别检查。未向系统窗口注入输入。
- C10 在上述三个截图条件中增加角色长按拖动/缩放，按下、移动、释放在一帧前全部入队，用采集时间证明延迟批次仍识别长按。真实模型 CPU 测试确认短按释放触摸、长按不触摸；数值测试确认缩放锚点及手势取消，截图验证位移与缩放变化。
- 实际 KPackage 六项测试通过：正常截图、两种 DPI 生命周期、错误素材、错误配置、恢复；测试资产/config 在证据目录隔离。
- 独立 Qt-only 宿主动态加载 stage 中的两份库，进程 maps 核实来源；真实背景/图标/Chocola 截图目视通过，未借用开发库搜索路径。
- Chocola date-a 同条件初态与只读 WebGL 对照：800×1080，原始 RGBA 最大差 `[1,1,1,0]`，容差 2 的超限像素 0；不扩大为所有模型或动态认证。
- UI GL 后端预热后 120 次绘制的 C++ new 计数为 0，两次资源初始化均检查。仅覆盖该后端，不代表全应用、字体首次内容变更、驱动或音频线程零分配。
- DPI 重复检查还修正了测试回放坐标：使用 wallpaper item 的逻辑尺寸，避免窗口管理器改变顶层窗口尺寸影响输入换算；实际 Qt 壳已使用 item 尺寸。
- 最终重复截图检查曾因非聚焦测试窗口的呈现同步降至约 1 FPS 而超时；trace 显示仍在推进。本轮截图测试单独关闭垂直同步，默认环境失败记录保留，不计为发布态性能结果。
- 临时 XDG 目录重装两次，配置字节保持不变。未在真实 miku/plasmashell、多屏或长期运行环境中验收。

## miku 一次安装流程

当前已构建的包为 `build/dev/adapters/plasma/stage`。在 **miku 用户**的终端执行（工作目录不限）：

```sh
bash /run/media/chocola/DATA/Project/NekoMote/tools/plasma_reinstall_user.sh \
  --stage /run/media/chocola/DATA/Project/NekoMote/build/dev/adapters/plasma/stage \
  --assets-dir /run/media/chocola/DATA/Project/NekoMote/assets
```

安装后重新登录该测试用户，以加载新 native 库和环境；在壁纸配置中选择 NekoMote 并设置上述 assets 目录。右上角猫爪/菜单图标打开共享面板，在设置中选择背景；首次配置未选择背景时显示底色是预期行为。重装保留原偏好。

需要重建时：`cmake --build build/dev --target nekomote_plasma_stage -j4`。不要把其他目录中的旧 stage 当成本轮包。

## ABI 与后续边界

高层 ABI 足以连接角色/背景/UI 绘制。后续用户补充的长按手势已将当前 ABI 更新为 0.4.0：pointer_move/mouse_button 携带采集时的单调毫秒时间，避免延迟批次把长按当短按；仓库内 C/Qt 调用者已迁移，安装时一并替换插件库并重新登录。但 G31/G32 的完整架构验收仍未闭合：player 与 WallpaperInstance 仍分别构建面板业务；Plasma 固定 33ms 定时器没有响应面板 FPS 设置；面板静音只改配置，没有完整联动当前设备；渐变使用墙钟时间；资源语义 ID 的设备映射及其他 ABI 异常边界仍需审查。详见 [当前缺口](known-defects.md)。真实桌面通过后再分派这些明确任务，不能把“UI 可见”当作所有控件行为已验收。
