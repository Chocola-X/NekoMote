# KDE 帧率与布局保存修复

2026-10-13，Codex，基线 `6e9bd21`，接续上一执行者的未提交保存代码。
本轮保留有效工作，完善共享 application/ui 的恢复逻辑；验收时未更新当前用户桌面安装。

## 问题与实现

当前 `plasmashell` 的 `/proc/1005/maps` 指向
`/home/chocola/.local/share/nekomote/lib/libnekomote_plasma_qml.so`。
该库缺少 `nekomote_wallpaper_fps_limit` 导出，新构建包含该接口。
旧安装库 SHA-256 为 `9a300ac01e5e88ab80ae728874d649e639b8ec13409373e6eb6ee16d1169fafb`；
本轮 stage 库为 `107b57aa4d67b1c71e5bc7fe5af26e737b4b8e8acc8a2c3fd043cf89875584a4`。
因此编译目录变更不能自行使运行中的桌面生效。

- 默认帧率统一为 60 FPS；已有 30/60/120 或无上限偏好保留。
  ABI 组合层从配置初始化菜单值，Qt 渲染线程通过原子快照发布帧率，GUI 定时器使用它。
  player 也保留保存的无上限设置。Qt 普通属性变更不再重新扫描和读取整个配置。
- 角色拖动释放、滚轮缩放、fit/reset，以及 UI 拖动结束持久化。
  Controller 在首次有效输出尺寸到达时恢复角色状态；默认适配位置单独作为 reset 基准。
  恢复时不将保存的变换作为默认基准，也不依靠壳重复执行 fit 覆盖保存值。
- 角色有效性根据实际绘制几何与当前居中输出矩形的相交判断，保留有意的部分裁切。
  完全移出视图、非有限位置或非法缩放回到当前输出的默认适配位置。
  输出后到和 resize 使用同一应用逻辑；切换到其他角色清除前一角色变换。
- 共享 UI 校验菜单和按钮的完整矩形，越界或非有限坐标回到默认位置。
  面板恢复后清除自由位置标记，纠正值写回配置；菜单内容高度变化后的检查也使用最终尺寸。
- 配置字段按类型读取，损坏的布局字段不会阻止有效角色、背景和 FPS 偏好加载。
  完整写入临时文件后替换配置，避免先截断原文件。输入回调中的角色保存失败进入可检查错误状态。
  `--no-save` 在初始化及恢复时有效。

## 验证

构建：`cmake --build --preset plasma -j 4`，Release/LTO/WERROR。

- `[controller],[ui_layout]`：39 用例、535 断言通过。新增合成素材用例覆盖首次/晚到尺寸、
  重开保留变换、reset 默认基准、小窗口越界回退、非法值、配置字段损坏及 no-save；
  UI 正/负坐标、右/下边界和合法位置保持均有用例。
- CPU 全 runner：194 用例、43631 断言通过；其中旧真实素材导入用例因
  `NEKOMOTE_TEST_REAL_ASSET` 未设置只输出警告并返回，本轮该导入检查未执行，不计为实测通过。
- ABI CTest：`abi_negative_test`、`abi_test`、`abi_synthetic_boundary`，3/3 通过。
- GPU `[gpu][ui],[gpu][abi]`：4 用例、90 断言通过。UI 每代预热后的 120 帧含
  新的闲置布局检查，C++ new 为 0，两代资源均验证；不代表全应用/驱动/声音零分配。
- Qt 壁纸集成使用隔离临时素材目录、Chocola date-a 和应用自身事件回调，
  GL 与 Vulkan 各通过 FPS 120 的菜单选择、配置写入、8ms GUI 定时器、重开恢复，
  角色/按钮/菜单拖动与缩放保存、布局损坏回退、属性变更、错误资源恢复。
  Vulkan 同实例 GL/VK 后端切换也保持布局。未向系统窗口注入输入。
- Stage 的文件/manifest/RPATH/动态库来源/符号及无 `LD_LIBRARY_PATH` 的动态加载恢复检查通过。
  可重装目录：`build/plasma/adapters/plasma/stage`。

原始材料在 `build/evidence/layout-persistence-20261013/`；二进制 hash 见
[冻结摘要](validation/layout-persistence-results.json)。GPU 检查通过 `build/gpu-validation.lock` 串行。
60 FPS 下旧测试固定等待 12 帧不足以完成 280ms 菜单动画，现按 350ms 墙钟等待；
新增菜单位移后，原角色平移比较区域包含了固定 UI，现将角色比较区域起点从 x=280
移到 x=340（菜单右边界 x=312），未放宽图像阈值。失败日志单列保留。

保存 I/O 仅在初始化、交互结束、缩放/fit/reset 和需要纠正布局的边界发生。
本轮没有测 CPU 帧时间/显存/功耗，不宣称性能收益。本机 Qt 的隔离测试
不能替代真实桌面的重装验收、多屏或长期运行。

## 当前桌面启用

在仓库根目录执行：

```sh
python3 tools/plasma_deploy.py reinstall --stage build/plasma/adapters/plasma/stage --restart-shell
```

这会更新用户插件并重启 plasmashell；仅重编译或关闭设置菜单不会卸载旧 native 库。
重装工具保留用户业务配置，正式启用后需要用户检查当前桌面的保存与恢复行为。
