# 受控原 WebGL 采样

这是开发诊断工具，不是正式播放器依赖。只读使用外部参考项目，不复制或改写其驱动。浏览器和 Node.js 路径由开发者显式提供。playwright 1.56.1 及依赖由 package-lock.json 固定。

```bash
npm ci --prefix tools/reference_capture
python3 tools/reference_capture/capture.py --reference-root ../NekoWebShow \
  --model chocola-date-a --browser /path/to/chrome \
  --output build/evidence/reference/default
# 可追加 --variables '{"head_LR":30}' --steps 60 --step-frames 1
# --trace 导出诊断几何/状态；--isolate-icon 0003 仅保留原驱动某图集区域的绘制
```

默认 800×1080、scale=1、coord=(0,400)、零固定步进。禁用 RAF，关闭 hair/parts/bust 物理与风，绕过应用反应规则，以全新 player 采样。保留原驱动渲染纹理及 WebGL 合成方式，包括透明颜色保护的两遍绘制。截图空白、页面错误或 GL 错误会失败；元数据记录源码 hash、默认变量、输入、GPU 和可见像素数。

隔离图集区域只用于基础 UV/alpha 对照；其过滤依据是诊断观测，不认证场景图或网格求值。trace 是验证数据，禁止作为正式原生运行时的顶点/动画数据来源。完整模型验证必须使用未过滤画面。

P3 支持两项额外诊断：`--trace-math` 记录原引擎的 sqrt/sin/cos/atan2 调用；只在显式启用时替换 Math 入口，不能用于性能测量。`--disable-clamp` 在内存生成 PSB v4 测试 fixture，仅把 `/metadata/clampControl/*/enabled` 从 1 改成 0，记录每个字节、原/新 PSB hash。原文件保持只读，此类截图不是未经修改的原模型验收结果。默认命令仍使用原 PSB；最终控制规则必须通过原 PSB 的对照。
