# P1 阶段复核与交接

2026-10-03，复核后完成结构导入和目录审计。此前交接记录中的“语义已确认”和“全部退出条件已通过”缺少证据，本文件替代旧结论。

## 修复与边界

- 保留 motion 级参数表/优先级/索引、层类型与组合遮罩引用、angle/zx/zy/motion、稀疏帧和时间线轨道。
- `Field<T>` 明确区分缺字段、显式 null 和值；非法 bp 长度/非有限数/越界引用不再变成空网格。
- 支持多个图集，icon 键为 `texture/icon`；带符号 32 位颜色按原位模式保存。
- 缺失 resx、资源/引用未解析、路径穿越、错误类型和必需结构错误使导入失败，并保留来源/字段路径。
- 外置纹理必须通过 `--texture name=relative-path` 显式映射。目录审计工具生成并记录无歧义的文件映射，不在运行时猜测文件名。
- CLI 产出当前 schema=1 的 `model.nkm.json`、引用的纹理、`report.json` 和离线 `source.json` 审计归档。运行时只加载当前项目资产，不读取归档；旧版本明确拒绝并要求重新导入。
- 尚未支持的源控制/物理/其他字段进入 `unsupported_fields`；完整源数据保存在离线归档供后续导入开发。结构导入通过不认证播放能力。
- core/model 仍只依赖标准库；预览关闭时不再配置 GLFW，测试关闭时不再配置 Catch2。

## 验证

新机器：GCC 15.1.1，CMake 4.0.3，Ninja 1.12.1；NVIDIA RTX 3060 Laptop / 575.64.03，X11。旧机器性能/显卡信息不作为本次基线。

```bash
cmake --preset dev
cmake --build --preset dev -j 4
NEKOMOTE_TEST_REAL_ASSET=../NekoWebShow/model_conversion ctest --preset dev
# 28/28，通过；包含主模型与错误输入/往返/拒绝覆盖/损坏资产回归
cmake --preset asan
cmake --build --preset asan -j 4
NEKOMOTE_TEST_REAL_ASSET=../NekoWebShow/model_conversion ctest --preset asan
# 28/28，ASan + UBSan 通过
python3 tools/import_emote/audit_catalog.py --reference-root ../NekoWebShow \
  --importer build/dev/bin/nekomote_import --output build/evidence/catalog-audit.json
# 195 条，193 条结构导入通过；0 条认证完整播放
```

目录审计的可提交紧凑结果见 [catalog-audit.json](catalog-audit.json)，完整诊断在忽略的 `build/evidence/catalog-audit.json`。两条失败：`milk-winter` 无解包 JSON；`vanilla-lolita` 的 `/source/tex/icon/0034` 超出图集范围，明确拒绝而不截断。`milk-teenage.pure.json` 别名及重复目录的选择均记录来源。

主模型：44 motions / 431 layers / 1892 frames / 80 icons / 1 atlas / 53 variable markers / 468 bp / 0 cc。新增测试确认参数表实际位于 motion，而不是仅测合成样例中的 layer.parameter。

## 语义和参考证据

详见 [semantics.md](semantics.md)。双三次 Bézier、继承位、变形组合、遮罩和差分求值仍待受控实验，不沿用之前仅根据工具枚举作出的确定结论。customPartsOrder 的字符串数字不能直接当成当前 motion 的绘制顺序。

`tools/reference_capture/` 使用外部原驱动，只读提供 PSB 和脚本，以固定输入绘制原 WebGL 画面，关闭物理与风，不执行反应脚本、不依赖墙钟。截图记录来源 hash、变量、步数、分辨率、GPU，空白截图和 GL 错误会失败。可选 trace 仅用于观察几何；禁止将其输出当作原生求值器的数据替代品。

默认参考截图：`build/evidence/reference/default.png`。已目视核实主模型画面非空、角色完整；这只是参考链验证，尚无原生整模型对照。

## 下一步

P2 静态 GPU 基础：独立纹理解码、最小绘制协议、显式宿主 target、单部件截图、alpha/UV/resize/重复生命周期回归。后续通过 P3/P4 的完整模型图像门槛后，才能宣称可完整渲染模型。
