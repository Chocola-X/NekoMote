# V5 播放器 UI 视觉、交互与状态机验证报告

- 记录日期：2026-10-07
- 执行者：Antigravity
- 模型：Gemini 3.8 Flash (High)
- 上下文：`docs/agent-workboard.md` V5
- 关联提交：`869a960` 及本次小型修复

---

## 1. 任务背景与对标基准

本任务对标 `../NekoWebShow/` 原生网页版本（`ui.css`、`ui.js`）的视觉表现与交互行为，验证并解决播放器预览窗口控制 UI 的视觉还原度、拖动稳定性及状态机时序问题。

用户指出的两项关键问题：
1. **菜单拖动时标题文字滞后**：拖动时顶部文字“猫娘乐园角色 E-mote 图鉴”出现移动延迟，表现为脱离原本菜单栏位置；
2. **菜单拖动后淡入淡出失效与关闭延迟**：自由拖动后淡入淡出效果未显现，且关闭菜单有明显视觉延迟。

---

## 2. 根因确诊与小型修复（`apps/player/ui*`）

### 2.1 问题 1：标题文字与面板坐标脱节滞后
- **根因分析**：
  在早期实现中，`main.cpp` 先使用旧坐标绘制面板圆角背景，随后在 `sidebar_header` 回调中响应鼠标更新坐标并绘制文字，抽屉项则沿用更新后的坐标。这种跨函数分散式更新导致面板背景、标题文字和子列表分属不同的坐标状态，在连续拖拽帧中造成标题文字滞后脱离面板 1 帧的视觉撕裂。
- **修复方案**：
  将拖拽判定与绝对坐标 clamp 统一前置在 `Context::sidebar(...)` 内部最顶层。绘制面板背景、标题文字 `sidebar_header`、抽屉和滚动条时，全部使用同一个严格同步的 `sidebar.x` 和 `sidebar.y`，标题文字偏移严格绑定为 `sidebar.x + 16.0f`、`sidebar.y + 12.0f`，实现 **0 像素同帧绝对同步**。

### 2.2 问题 2：自由模式淡入淡出与关闭卡顿
- **根因分析**：
  1. 原渲染器缺少全局透明度支持，图元均为全不透明硬绘制；
  2. 原动画使用无时间基准的静态插值 `lerp(a, target, 0.18)`，渐变尾部衰减缓慢（需超 25 帧才到达阈值），造成关闭时无视效停滞近半秒后突变消失；
  3. 对齐 NekoWebShow 规范：菜单被拖动为自由态（`is_free = true`）后，CSS 规则 `transform: translateX(0); transition: opacity 0.28s ease` 取消横向滑出，只执行原地淡入淡出。
- **修复方案**：
  1. 在 `Renderer` 中建立 `set_global_alpha(float)` 阶梯级联，圆角矩形、文本、图标、分割线和三角形全部乘以当前 `global_alpha_`；
  2. 采用精确 `delta_time` 线性过渡：展开耗时约 0.28s（`open_speed = 1.0f / 0.28f`），关闭耗时约 0.24s（`close_speed = 1.0f / 0.24f`），并在 `< 0.001f` 时归零截断，彻底消除拖动后的关闭悬停迟滞；
  3. 在 `is_free = true` 态下，锁定 `sidebar.x` 和 `sidebar.y`，仅推进 `animation` 实现原生平滑淡入淡出。

### 2.3 边界安全性与输入隔离优化
1. **视口越界安全恢复**：
   对齐 NekoWebShow `resetTopbarToInitial` 逻辑：当自由态菜单处于隐藏态且窗口缩放导致其坐标超出视口时，重新展开时自动重置为停靠初态（`is_free = false, x = 8, y = 8`）。
2. **图标拖动释放防御**：
   在 `toggle_icon` 中加入 `if (mouse_released_ || !mouse_down_) icon.dragging = false;`，防止鼠标移出窗口后释键导致拖动状态无法复位。
3. **渲染器无 GL 环境安全守卫**：
   为 `Renderer` 所有 `draw_*`、`flush_*` 及 `begin/end_frame` 增加 `if (!initialized_) return;`，防止无窗口/纯 CPU 环境下空指针解引用，使状态机单测可完全在 CPU 运行。
4. **输入捕获评估辅助**：
   在 `Context` 中提供 `eval_captures_mouse(const IconState&, const SidebarState&)`，高度安全使用 `std::max(sidebar.height, 44.0f)`。

---

## 3. 自动化状态机单元测试（`nekomote_ui_tests`）

根据 `agent-workboard.md` 规范，UI 测试独立构建，**不混入引擎核心 136 项测试**。

新建独立测试工程 `apps/player/test_ui.cpp`，构建目标 `nekomote_ui_tests`（Catch2 v3.5.3），包含 8 组高强度无窗口断言测试：

| 测试用例名称 | 验证重点 | 结果 |
|---|---|---|
| `UI Sidebar docked animation open and close timing` | 验证停靠态展开（~0.28s）与关闭（~0.24s）时序，终态无死循环尾巴 | **PASS** |
| `UI Sidebar dragging coordinates and synchronous title position` | 验证拖拽帧位移同帧无缝对齐，标题文字 0 滞后 | **PASS** |
| `UI Sidebar dragging viewport bounds clamping` | 验证上下左右拖出视口时强制限制在 [8, vw-w-8] 和 [8, vh-h-8] | **PASS** |
| `UI Sidebar free fade-in / fade-out (in-place opacity transition)` | 验证自由拖拽态关闭与重开时坐标保持锁定，仅透明度过渡 | **PASS** |
| `UI Sidebar mid-animation reversal` | 验证动画进行中途突然反向展开/关闭时顺滑倒退、无突变跳帧 | **PASS** |
| `UI Input capture state machine (eval_captures_mouse)` | 验证悬停图标、悬停菜单、拖拽中捕获鼠标，空闲态放行 | **PASS** |
| `UI Icon toggle vs drag distinction` | 验证点击（<3px）触发切换，拖动（>3px）移动图标且不触发开关 | **PASS** |
| `UI Viewport bounds safety reset for free sidebar` | 验证移出视口后重新展开时安全重置回左上角停靠态 | **PASS** |

**测试执行汇总**：
- `build/dev/bin/nekomote_ui_tests`：**8 passed / 0 failed, 124 assertions passed**。
- `ctest --preset dev`：**136/136 PASS (100%)**，核心与 ABI 测试完全不受影响。

---

## 4. 视觉对标与截图存证

使用真实已验证模型 `chocola-date-a` 捕获应用内 UI 截图（保存于 `build/evidence/v5-next/`）：

1. **菜单收起状态**：
   - 文件：`build/evidence/v5-next/closed.png`
   - 画面：右上方 44px 粉色肉垫猫爪图标，无菜单残留遮挡，角色完整。
2. **角色抽屉展开状态**：
   - 文件：`build/evidence/v5-next/open-character.png`
   - 画面：264px 半透明面板，左上粉色圆角边框；“巧克力”抽屉展开显示“日常·原版”等列表，“约会·站姿 A”呈选中高亮粉底色；细粉色滚动条位于右侧。
3. **香草抽屉展开状态**：
   - 文件：`build/evidence/v5-next/open-vanilla.png`
   - 画面：上部“巧克力”折叠并带有向下箭头指示，“香草”展开，显示无铃铛等差分项。
4. **设置抽屉展开状态**：
   - 文件：`build/evidence/v5-next/open-settings.png`
   - 画面：帧率单选圆点（无上限/30/60/120），带“推荐”徽标；锁定人物开关；关闭声音开关（受 `--mute` 正确驱动点亮）；粉色“重设角色位置”按钮；固定背景单选列表；底部作者与项目署名。

---

## 5. 发现的输入隔离缺陷与交付建议（面向 Codex）

根据规范要求：“UI捕获输入时不能触发模型，外部视线/触摸应正常。禁止改动Codex拥有的main/runtime/ABI。缺接口则报告所需最小入口。”

**审查发现的 Codex 侧入口缺陷**：
在 `apps/player/main.cpp` 的 `Surface::move` 回调函数中（line 342–359）：
```cpp
static void move(GLFWwindow* w, double x, double y) noexcept
{
    auto& s = get(w);
    const auto p = s.point(x, y);
    if (s.ui_context)
        s.ui_context->mouse_move(x, y);
    if (s.dragging && !s.locked)
    {
        s.origin.x += p.x - s.previous.x;
        s.origin.y += p.y - s.previous.y;
        s.accept(s.session->set_origin(s.origin));
    }
    s.previous = p;
    Vec2 gaze = p;
    if (s.mirror)
        gaze.x = -gaze.x;
    s.accept(s.session->pointer_move(gaze)); // <-- 缺陷：未检查 captures_mouse()！
}
```
**现象**：
在 `button` 和 `scroll` 回调中均已有 `if (s.ui_context && s.ui_context->captures_mouse()) return;`，防止了点击和滚轮穿透到模型。但 `move` 函数在第 358 行**未做捕获检查**，导致鼠标在 UI 面板或图标上移动时，注视坐标（gaze）依然被派发给底层模型，模型眼球会跟随 UI 上的鼠标光标转动。

**建议 Codex 后续采纳的最小修复**：
在 `apps/player/main.cpp` 的 `Surface::move` 中，在更新 UI 鼠标坐标和计算完 UI 捕获状态后，加入拦截：
```cpp
if (s.ui_context && s.ui_context->captures_mouse())
    return;
```
即可彻底保证 UI 操作期间模型注视保持稳定，不被 UI 上的光标牵引。
