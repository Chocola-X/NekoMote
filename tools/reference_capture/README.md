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

--fixture-icon-size tex/0077=213,213 只在内存修改所选 atlas icon 的 float32 宽/高，保持地址表和未选载荷不变；记录修改字段、字节范围与 hash。仅用于网格尺寸边界实验，不能替代原 PSB 的正式验收。外部目录始终只读。

`--trace` 另存 `.state.json`：目标附件、颜色位数、纹理类型/尺寸、blend、scissor、viewport 与 clear。`--trace-mask-pixels` 需同时启用 trace，额外导出 mask green 的直方图与非零像素；是显式 GPU 读回诊断，不用于性能或原生帧输入。

完整静态图像、实际原生几何与拓扑的 30 组验证入口：

```sh
python3 tools/reference_capture/validate_static.py --reference-root ../NekoWebShow \
  --importer build/dev/bin/nekomote_import --preview build/dev/bin/nekomote_preview \
  --evaluator build/dev/bin/nekomote_pose --browser /path/to/chrome \
  --output build/evidence/static-validation
```

输出目录必须新建。默认重新导入四型号并采样原 PSB；也可显式指定 `--reuse-reference <既有采样目录>`，此时核对原始素材、驱动、资产与 PNG hash，以及初态、变量和物理配置。工具不覆盖证据，逐组记录全部结果，有任一失败则最终返回非零。当前有已知 FAIL，见 `docs/validation/p4-static-images.json`；阶段/完整播放认证始终保持 false。
