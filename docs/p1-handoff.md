# P1 阶段交接记录

阶段：P1 素材审计、语义清单与导入器
日期：2026-10-03
状态：完成（主样例 chocola-date-a；完整 195 条目录审计推迟至 P9）

## 目标

建立项目自己的强类型模型 schema，实现离线导入器把 E-mote PSB JSON 转成 ModelAsset，并以主样例 chocola-date-a 验证结构与资源引用（ROADMAP §6）。

## 调查结论（E-mote 求值语义）

通过 refer/FreeMote-SDK、FreeMote.NET（MmoBuilder/MmoEnums）与 NekoWebShow driver 调查，确认以下语义（证据见 docs/p1-handoff.md 与代码注释）：

- **mesh.bp**：4×4 控制点，行主序 32 float，(x,y) 对，归一化 [0,1]² 参数空间。默认值 (col/3, row/3)。`meshTransform=1` 即 BezierPatch。
- **求值**：双三次 Bézier patch，P(u,v)=ΣΣ B_m(v)B_n(u)C[m][n]，方向为参数域→曲面。
- **meshDivision**：每边细分段数，生成 (N+1)² 顶点。
- **帧间插值**：对 bp 控制点逐分量线性插值再求值（与先求值再插值顶点数学等价）。
- **parameterize**：指向 part 的 parameter 表索引，parameter 含 {id, rangeBegin, rangeEnd, division, enabled}。
- **icon**：三种形态——纯数字 '0003' 指向 source.tex.icon 字典（left/top/w/h/origin/attr/zorder）；'x:y:w:h' 内联像素矩形；名称引用。
- **content 字段集**：bm/color/coord[3](x,y,z 深度)/icon/mask/mesh{bp,cc}/opa/src。
- **参数默认**：从 PSB variable/frame 表读初值；config.js 是反应脚本不进默认值。
- **绘制顺序**：metadata.customPartsOrder 是部件 ID 数组，按序从前到后绘制。

## 未验证假设（需 P3/P4 受控验证）

- Bézier 边界外行为（bp 可超 [0,1]）、帧间 easing、参数→帧 time 映射、meshTransform.Composite=2 组合顺序、cc 语义（此模型全 None）、层变换组合顺序（transformOrder）。

## 交付

- `include/nekomote/model/asset.hpp`：强类型 ModelAsset schema（IconRect/TextureDef/MeshData/FrameContent/LayerFrame/ParamBinding/Layer/Part/VariableDef/ModelAsset），用 std::optional 保留 null/0 区别。
- `include/nekomote/asset_io/import_emote.hpp` + `src/asset_io/import_emote.cpp`：导入器，null 安全的 get_int/get_uint64 helper，解析顶层/metadata/source/object 树。
- `tools/import_emote`：CLI，输出能力报告。
- `tests/fixtures/minimal.{json,resx.json}`：合成样例。
- `tests/unit/test_import.cpp`：6 个合成样例测试 + 1 个真实样例测试（环境变量门控）。

## 执行命令与结果

```bash
cmake --preset dev && cmake --build --preset dev
NEKOMOTE_TEST_REAL_ASSET=/run/media/chocola/MIKU/NekoWebShow/model_conversion ctest --preset dev
# 23/23 通过（含真实样例 chocola-date-a 导入，0.35s）

cmake --preset asan && cmake --build --preset asan
NEKOMOTE_TEST_REAL_ASSET=... ctest --preset asan
# 23/23 通过（ASan+UBSan）

clang-format -i $(git ls-files '*.hpp' '*.cpp')  # 格式化
clang-format --dry-run --Werror $(git ls-files '*.hpp' '*.cpp')  # exit 0
```

真实样例 CLI 报告：
```
spec: ems  version: 3.03  screen: 800x1080
parts: 44  layers: 431  frames: 1892
icons: 80  textures: 1  variables: 53
bp_nonnull: 468  cc_nonnull: 0
texture_path: chocola-date-a-resources/tex-texture.png
```

## 退出条件核对

| 条件 | 状态 |
| --- | --- |
| 主样例资源引用与图集信息正确 | ✓ texture_path、80 icons、图集 4096×2048 |
| 结构规范化确定且可复现 | ✓ 23 测试固定结构 |
| schema、字段证据记录格式与导入错误契约有文档 | ✓ 本文件 + 代码注释 |
| 保留 null/缺字段/0 区别 | ✓ std::optional + get_int helper |
| 不以删除 mesh/遮罩取得"导入成功" | ✓ bp/cc/inherit_mask/stencil_type 全保留 |

## 当前契约

- `nekomote_model`：asset schema 加入，仍仅依赖标准库。
- `nekomote_asset_io`：链接 model + nlohmann_json，公共头不泄漏 json。
- 导入器不抛异常，错误进 ImportReport.errors。

## 下一阶段（P2）

P2：原生静态绘制基础（ROADMAP §7）。首个任务：实现 PNG 图集加载上传 + 静态顶点/UV/索引缓冲 + 单帧绘制。引入 glad GL loader。以合成样例和无变形部件验证 UV/排序/alpha，再渲染 chocola-date-a 的无变形部件。
