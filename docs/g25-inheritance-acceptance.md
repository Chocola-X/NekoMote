# G25 候选扩大验收报告

**Agent**: CodeAr(arts) / GLM-5.2 | **Base**: d297827 | **Branch**: g25-acceptance | **Date**: 2026-10-09

## 1. 静态几何重验（27 条件）

三代表 × {default, body_LR±5/±15/±30, body_UD30, body_SL30} = 27。

**27/27 通过**，max_error = 1.36e-4 px（阈值 1e-3），顶点/三角形/命令/遮罩全匹配。
binary hash 与 Codex 一致（同 d297827 源码）。参照 trace hash 已核验。

## 2. GPU 对照（27 条件）

Codex 已验 5（coconut bodyLR30/±30/UD30, azuki bodyLR30, cinnamon bodyLR30）。
本轮补 22：三代表其余全部条件。

**22/22 通过**，alpha 差 0，黑白合成 RGB 差 ≤1，零像素超容差 2。bbox 完全匹配。
GPU 在共享 flock 内串行。参照图 hash 已核验。

## 3. 195 初态扫描

**195/195 加载成功**。15 旧 FAIL（0x7fc inherit_mask）全部修复，0 新回归。
加载 ≠ 视觉通过：三代表已有 WebGL 参照验证（§1+§2）；其余 12 旧 FAIL 型号无参照，仅验证加载。
嵌套排除仍拒绝（geometry_scene.cpp:296 `!inherit_shape && parent_partial_shape`）。

## 4. 扩组合参数（45 条件）

三代表 × 15 组合（head_LR±30, head_UD30, body+head 组合, eye/move/bust/mouth 等）。

**45/45 加载成功**，非平凡输出（顶点/索引/primitives > 0）。
bbox 响应 24/33：body/head/move/slant 变量改变 bbox；face_eye_LR/face_mouth/bust_LR 不改变整体 bbox（局部变化，符合预期）。
无浏览器可用，新 WebGL 参照未生成；需参照的条件标 Skipped。

## 5. 性能基准

CPU evaluate+build_frame，预热 120 步，采样 4000，固定 pose body_LR=30，无 renderer/UI/audio/readback。

| 模型 | P50 (ms) | P95 (ms) | P99 (ms) | nodes |
| --- | --- | --- | --- | --- |
| coconut-santa | 0.383 | 0.410 | 0.452 | 489 |
| azuki-santa-b | 0.309 | 0.341 | 0.404 | 522 |
| cinnamon-maid | 0.292 | 0.352 | 0.396 | 457 |

与 Codex) 基准一致（coconut P50 0.383 vs 0.374）。scope 仅 CPU 求值，不含 renderer/application/UI。

## 6. 证据冻结

`build/evidence/g25-acceptance/frozen-manifest.json`：
源码/binary/asset/tool/参照/结果 hash 全部冻结。逐条 argv、stdout/stderr 保留。
manifest hash: a1f6c925fc0e52d96f0f9d81c910d94b6d6db92ac4be14907741d5c372323468。

## 7. 未验证项

- 12 旧 FAIL 型号无 WebGL 参照，仅验证加载
- 扩组合新条件无浏览器生成参照
- sync1/8、固定步 idle/动作/物理恢复、locator/触摸投影未测（需动态工具/浏览器）
- renderer/application/UI 稳态分配未测（nekomote_pose 仅 CPU 求值）
- 嵌套排除语义仍需独立证据

## 结论

候选 d297827 在三代表 27 静态几何 + 27 GPU 像素级通过，195 初态全加载无回归，扩组合 45 条件加载成功，性能与 Codex 基准一致。嵌套仍拒绝。交 Codex 最终复核决定正式合入。
