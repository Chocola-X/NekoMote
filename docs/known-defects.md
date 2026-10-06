# 已知渲染缺陷：coconut 系列模型手部水平拉伸

## 发现日期
2026-10-06

## 问题描述
coconut 多个模型的手部存在水平方向的错误拉伸。手部放在臀部附近的立绘更容易出现此问题，特别是手部和臀部纹理分开处理时。需要与参考的 asm.js 引擎渲染画面比较确认。

## 影响范围
- coconut-casual（已确认与参考画面有 93% 像素大差异）
- coconut 其他多个模型（用户目视确认）
- 可能影响其他角色系列中手部放在臀部附近的型号

## 已完成的调查

### 1. 参考画面捕获（成功）
使用 `tools/reference_capture/capture.py` 成功捕获了原项目 NekoWebShow (asm.js/WebGL) 的 coconut-casual 参考渲染画面：
```bash
python3 tools/reference_capture/capture.py \
  --reference-root /run/media/chocola/DATA/Project/NekoWebShow \
  --model coconut-casual \
  --browser <chromium-headless-shell-path> \
  --output /tmp/coconut-ref
```
参考画面：800x1080，421900 可见像素。

### 2. NekoMote 渲染（成功）
```bash
build/release/bin/nekomote_player --assets assets --mute --character coconut-casual \
  --capture /tmp/coconut-native.png --steps 0 --transparent --no-save \
  --width 800 --height 1080 --scale 1 --origin-x 0 --origin-y 400
```
NekoMote 画面：800x1080，864000 可见像素（背景未透明，transparent 在 assets 模式下有背景自动加载问题）。

### 3. 差异分析
- 在参考画面可见的 421900 像素中，93% (393133) 与 NekoMote 有大颜色差异（RGB 差异 > 30）
- 差异随 y 坐标增大而加剧，集中在角色下半身（y=850-1050 峰值 36631 差异像素/50行）
- 这与用户描述的"手部放在臀部附近"区域一致

### 4. 工具能力评估
- ✅ `capture.py` 可成功捕获原项目 WebGL 渲染画面
- ✅ `compare_images.py` 可比较画面差异
- ❌ `analyzeImage` 多模态分析工具不可用（无可用多模态模型），无法目视确认手部拉伸
- ⚠️ NekoMote player 的 `--transparent` 在 assets 模式下未正确禁用背景（背景自动加载），影响精确比较

## 根因推测
手部拉伸可能与以下相关：
1. **mesh.bp/cc 求值语义**：AGENTS.md 已标记"最大的未知是 E-mote 的准确求值语义，尤其 mesh.bp/cc"。手部变形可能涉及 mesh 变形，求值公式可能与原引擎不一致。
2. **遮罩处理**：手部与臀部重叠时可能涉及遮罩混合，遮罩区域或混合顺序可能有误。
3. **parameterize/变形组合**：手部位置的参数化变形组合可能与原引擎不同。
4. **继承位**：手部可能继承自臂部/躯干的变换，继承链求值可能有误。

## 修复状态
**未修复**。需要以下后续工作：
1. 修复 player 的 `--transparent` 在 assets 模式下的背景自动加载问题，以获得精确的无背景比较
2. 用 `trace.js` 捕获原引擎的顶点/UV 数据，对比 NekoMote 的求值结果
3. 逐 layer 比较手部区域的变换矩阵，定位是 mesh 变形、遮罩还是继承位的问题
4. 用 `runtime_audit` 和 `pose_probe` 做受控变量下的逐顶点比较

## 相关文件
- 参考捕获工具：`tools/reference_capture/capture.py`, `trace.js`, `validate_static.py`
- 渲染器：`src/render_gl/renderer.cpp`
- 求值核心：`src/runtime/geometry_scene.cpp`, `src/runtime/animation.cpp`
- 模型资产：`assets/characters/coconut-casual/model.nkm.json`
- 项目规范：`AGENTS.md` §0.4（求值语义风险）

## 附：coconut-dress 纹理空白问题（已修复）
coconut-dress 的 resx.json 引用的 `coconut-dress-resources/tex-texture.png` 是全空白纹理（32KB，0% 非空像素）。从 `data/coconut-dress.pure.psb.zip` 用 PsbDecompile 解包后获得的正确纹理为 3.3MB（42% 非空像素）。已用正确纹理重新导入。其他模型可能也存在 resx 引用空白占位纹理的问题，需要全量检查。
