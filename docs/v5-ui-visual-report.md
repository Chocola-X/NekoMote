# V5 UI证据与复核

执行：2026-10-07，Antigravity / Gemini 3.8 Flash (High)，提交97c9409；Codex复核于同日。当前任务见 [workboard](agent-workboard.md) V6，现状见 [整合摘要](review-agents-current.md)。

## 实际改动

父提交5a4dc13已经具备拖拽先更新坐标、Renderer global_alpha以及.28秒展开/.24秒关闭。97c9409新增图标坐标clamp和失去按下状态后的释放、sidebar最小44px高度处理、越界重开reset及渲染无GL初始化守卫；另提供输入捕获评估辅助和独立UI测试。现有拖拽顺序及淡出实现不再算本轮新修复。

## 自动测试

`apps/player/test_ui.cpp` 独立8组、124断言，Codex复跑PASS。覆盖开关时序、拖拽坐标、clamp、自由态坐标保持、中途反向、捕获辅助、图标点击/拖动与越界重置。

这是无GL状态测试。标题同步测试计算 `sidebar.x+16` / `sidebar.y+12`，没有观察实际图元；淡出测试观察状态值和位置，没有观察最终alpha像素。因此不报告0px视觉误差或动态淡出已验收。UI测试尚未登记CTest，G9处理。

## 静态截图

已有 `build/evidence/v5-next/closed.png`、`open-character.png`、`open-vanilla.png`、`open-settings.png`，使用chocola-date-a展示收起、角色、香草和设置四态。截图证明这些静态状态已捕获；对应WebGL画面/边界/DPI以及连续拖拽和淡出帧需V6补受控对照。

## 剩余接口问题

`Surface::move`在更新UI鼠标后仍派发模型pointer_move，注视可能穿透；按钮/滚轮已有捕获检查。实际捕获状态在渲染帧更新，不能仅增加上一帧布尔检查便宣称全部输入隔离正确。由Codex统一当前坐标捕获和应用内回放入口，Gemini再提供逐帧视觉证据；不使用OS键鼠注入。
