# P2：原生静态 GPU 基础

2026-10-03，完成。这里只验证基础绘制，不认证完整模型播放。

## 实现

- asset_io 解码 PNG；render_gl 上传 RGBA8 纹理一次并独立拥有 GPU 生命周期。
- 绘制协议只含连续顶点/索引、强类型 TextureId、over 合成命令；后端不访问 E-mote 字段、JSON、窗口或桌面对象。
- OpenGL 3.3 + glad 2.0.8；宿主显式传入 framebuffer/尺寸。先 reserve，再 prepare 上传；超容量或非法引用明确拒绝，重复 render 复用缓冲。
- 输出约定：Y 向下、左上 UV、编码色值、目标预乘 alpha。截图独立读回并转换为 PNG 直通 alpha。
- 预览提供图集 icon、位置/缩放/透明度、隐藏窗口单帧截图及发布态提交基线。加载当前项目资产；缺少 --icon 时明确报告整模型求值未实现。
- 资源在同一图形线程/当前上下文释放；GLFW 窗口生命周期晚于 Renderer。初始化/解码失败经 RAII 回收；最小化目标不提交绘制。

## GPU 与图像证据

Debug / Release：30 个单元 CTest + 1 个 GPU CTest（含 3 个 GPU 测试用例），31/31 通过。ASan/UBSan：30/30 无窗口用例通过。

GPU 测试实际读回像素，覆盖线性过滤下的图集方向/alpha、透明命令保序、宿主 FBO、污染状态重设、128×128 resize、坏命令拒绝及 12 次 Renderer/纹理重建销毁。预热 120 帧后的 100 次 prepare/render 没有 C++ operator new/new[] 分配；截图和测试框架不参与测量。

原驱动的颜色保护双遍绘制必须同时保留；第一次隔离只留下 over 导致参考边缘变暗，修正诊断过滤后再验收。没有为迎合图像删除该参考行为。

真实主模型口部 `tex/0003` 与原 WebGL 隔离图对照：

| 条件 | alpha 最大差 | 黑/白背景 RGB 最大差 | 几何 bbox |
| --- | --- | --- | --- |
| 1×，opacity=1 | 0/255 | 0/255 | 完全相同 |
| 2×，opacity=1 | 0/255 | 1/255 | 完全相同 |
| 0.5×，opacity=0.5 | 1/255 | 1/255 | 完全相同 |

原始截图、局部放大与差值图在 `build/evidence/`；可提交的 hash/输入/数值报告在 [validation/p2-images.json](validation/p2-images.json)。1× 的原始直通 RGB 最大差也是 1/255。缩放后的透明像素反预乘会放大 RGB 量化误差，因此报告同时保留 raw_max_rgba，并以 alpha 与黑/白背景合成判断可见结果。未把这类差值隐藏或计为几何成功。

参考捕获保留原生驱动求值和绘制，只过滤已求值单个图集区域；原生位置来自测量后显式诊断输入。这证明 P2 的 UV、像素原点、缩放和 alpha，**不证明原生场景图或变形求值**。完整参考图 `build/evidence/reference/default.png` 已目视核实，P3/P4 必须对照未过滤整图。

## 首份发布态 CPU 提交基线

- 源码：`c38a0a4` + 本次 P2；renderer SHA256 `8aa0692d8e752cd935b8de7a5092ca1151ccaad8048155927f884f514167a5e2`。
- GCC 15.1.1，Release + LTO；NVIDIA RTX 3060 Laptop / 575.64.03，KDE/X11，GL 3.3 EGL。
- chocola-date-a `tex/0003`，800×1080，无背景，隐藏窗口，vsync=0，预热 120，采样 1000。
- render + swap：P50 **0.038064 ms**，P95 **0.049518 ms**，P99 **0.061042 ms**。
- 只测 CPU 提交和 swap；未测 GPU 完成、实际呈现、30/60 FPS 会话、显存与功耗；不据此宣布完整壁纸性能改善。截图不参与采样。

```bash
build/release/bin/nekomote_preview --asset build/evidence/chocola-date-a/model.nkm.json \
  --icon tex/0003 --origin 400 651 --hidden --benchmark 1000
```

## 下一步

P3：建立参数与展开场景的候选求值器，先以单变量端点、中点及组合输入验证 `mesh.bp`、继承、坐标和变形组合；P4 实现并核实遮罩。未确认语义不得作为正式加载 fallback。当前仍不能完整渲染 2D 模型。
