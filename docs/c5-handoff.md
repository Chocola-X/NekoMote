# C5：bm19/attr2原生映射与整模型验收

2026-10-07，基线 `8514d87`。执行：Codex / GPT-6。结果索引：[c5-blending-results.json](validation/c5-blending-results.json)。未认证整个目录或完成全部C5。

## 已交付

- 求值器将bm19编译为独立Multiply命令；未知bm仍拒绝。Blank只携带状态不绘制。bm19采用预乘源RGB、`DST_COLOR / ONE_MINUS_SRC_ALPHA / ZERO / ONE`，保持目标alpha；attr1不会让Multiply进入普通Over的双遍保护。attr2为普通单遍Over，未知attr仍拒绝。
- 混合仅支持已证明的ProtectedStraight组合。遮罩采样alpha独立于颜色混合；Add/Screen、未知组合、嵌套遮罩和未验证继承没有被放行。GPU测试覆盖透明/半透明/不透明目标、局部/全局opacity、后续Over状态恢复及100帧预热后项目C++零分配。
- 诊断预览的整模型opacity施加于最终合成，并应用资产mirror；pose对照也计入呈现镜像。**正式播放器原有mirror行为保持**，不要为消除诊断差异取消镜像。

## 实际验证

- 原115组WebGL矩阵重跑 **115/115**，不改冻结源/资产或容差。
- 新Fraise maid-a、Milk summer、Milk winter：固定clock=1000ms/seed=1、原PSB、物理启用，初态/60步/头身姿态60步 **9/9**。标量≤1e-5，最大几何误差0.00007003px，alpha和黑白合成RGB≤2/255。已阅读WebGL/原生联系图。
- V6原19组整模型核对 **10/19**。Fraise maid-a10组通过；非零步必须使用managed Playback及零物理幅度，孤立Animation诊断为手动控制器，不能混为同输入。错误诊断尝试保留；改用独立原生Playback后step30几何0px、合成像素零差异。
- Fraise france-maid-b9组仍FAIL：多数初态活动stencil无法解析；闭眼姿态可以输出，但在保留mirror后仍有约57.775px几何误差。attr2的单层状态已确认，**整模型不认证**。源样本的衣装由实际PSB决定，不能按型号文字猜测或替换素材。
- 默认 `--assets assets --no-save --mute --capture ... --steps 60` 成功，选中milk-summer/BG_30A，1920×1080画面已检查。恢复的是实际bm19，不是近似开关或自动更换角色。
- dev CTest138项、headless ASan/UBSan136项及无ABI Release/LTO无DISPLAY133项；GPU9组174断言。单次Release CPU Chocola2000步P50/P95/P99为0.274617/0.319665/0.350466ms，不能视为受控性能改善或整机功耗结论。

证据目录 `build/evidence/c5-blending-2026-10-07/`，包含FAIL和修正前尝试。Milk winter的本轮资产经canonical `milk-winter.pure.json`导入到该目录；没有覆盖用户assets或旧冻结资产。现有旧schema资产仍需显式重建。

## 复跑入口

```sh
# 实际结果应保持10/19；Fraise france-maid-b会使命令非零
flock -n build/gpu-validation.lock python3 tools/reference_capture/validate_blending.py \
  --reference-root ../NekoWebShow --inputs docs/validation/blending-inputs.json \
  --preview build/dev/bin/nekomote_preview --evaluator build/dev/bin/nekomote_pose \
  --playback-evaluator build/dev/bin/nekomote_playback_probe \
  --frame-renderer build/dev/bin/nekomote_frame_render --output build/evidence/c5-recheck
```

新完整播放请求冻结于 `blending-playback-inputs.json`，使用既有validate_playback流程；Milk winter通过canonical选择取得输入，不假设所有源码均为无后缀文件名。外部源仓库只读；不要读或移植私有asm函数，不注入OS键鼠。
