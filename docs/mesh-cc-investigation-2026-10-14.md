# mesh.cc 独立实验：尚未建立计算公式

用户要求先提交 C18，再尝试 cc。C18 已提交为 `94af01c`；C19 以该提交为基线，只修改诊断工具、测试和证据文档。**尚未找到能在生效路径产生有限且可辨识效果的非空 cc 布局，未实现或放行 cc 计算。** 当前 importer 的一维有限 float 数组类型只是保留数据的既有约束，不能视为原格式已经验证。

## 边界与输入

模型为程序生成的单个 32×32 四色棋盘，没有人物素材。PSB、JSON、resx 和 PNG 同源；PSB 很小是资源少，不影响此处字段求值实验。图集大小/偏移已有 C18 单独对照，不用增大纹理来修复 cc 数据布局。

没有读取、反编译、移植原求值核心；原网页驱动只按原样运行和取文件 hash。观察限于公开 SDK 输入、提交给 WebGL 的顶点/索引/状态及最终画面，禁用 shader 源码与内部执行观察器。外部原仓库只读，GPU 捕获通过 `build/gpu-validation.lock` 串行执行。公开 PSB/MMO 转换器中 cc→mcc 的字段映射及注释没有非空样本或求值公式，不能据其 ColorControl 注释推导颜色语义；未移植转换器代码。

## 受控结果

`probe_mesh_cc.py` 默认候选为 32 个零标量，保持 coord=(0,0,0)，参数 p 范围 0..60、division=60、连续模式，关键帧 time=0/type3→time=60/type2。BP 分别为空、恒定单位曲面、x 曲面系数 0.3→−0.2；cc 独立放在左端/右端/两端。

| BP / p=15 | 两端 cc=null | 仅左端有 cc | 仅右端有 cc | 两端有 cc |
| --- | --- | --- | --- | --- |
| 空 | 有限、1024可见像素 | 有限、1024像素 | 有限、1024像素 | 有限、1024像素 |
| 恒定单位曲面 | 有限、1024像素 | XY NaN | 有限、1024像素 | XY NaN |
| 曲面变化 | 有限、1024像素 | XY NaN | 有限、1024像素 | XY NaN |

另三个恒定单位 BP/左端 cc 对照：p=0、p=60、左帧改 type2/hold 后 p=15，均正常。共 **11 captured、4 非有限提交**，工具返回 **1**，不是 15 项语义 PASS。四个失败各有 100 个实际绘制引用的顶点，XY 共 200 个 NaN，±Inf=0；UV 与顶点颜色仍有限。所有正常非空 cc 组的坐标/UV/颜色/索引及 RGBA 与相应 null 对照相同（忽略缓冲区地址差异），没有确认 cc 的有效计算。

限定结论：**本轮候选在 BP 非空、type3 段内部、左端 cc 非 null 时产生 NaN。** 不能推广为所有非空 cc 均非法，也不能因为静态、无 BP 或右端单独设置时能出图，就认为语义已支持。它提示左端内容参与该区间的某条处理路径，但不足以确定字段用途、形状或公式。

此前 176 个探索记录（88 正常捕获/88 捕获失败）试探了长度4..256的若干扁平布局、嵌套控制点/分量布局、标量/布尔/对象、部分 mesh mask/transform 配置、easing 表及端点放置。它们不是有效格式覆盖率或验收矩阵；其中 meshTransform 非1失败不能归因为 cc，早期失败 trace 中 JSON null 也不能单独证明 NaN。探针修复后的最小矩阵才用于上述因果区分。最小矩阵初跑与最终冻结各15次，最终结果相同；重复捕获不增加语义覆盖。

额外交叉检查：`curve-layout/controls-128/model.json` 用用户指定的 PsBuild 编译为 `model.pure.psb`，同 p=15 条件仍提交 200 个 XY NaN；这排除了该候选失败仅由自制 PSB 编码造成的解释，不证明该候选符合有效 cc 格式。PsBuild 与自制 PSB 不要求逐字节相等。编译在生成模型目录及隔离 Wine prefix 内运行，没有修改用户 Wine 配置或外部素材。

## 复现与诊断交付

```bash
flock build/gpu-validation.lock python3 tools/reference_capture/probe_mesh_cc.py \
  --reference-root ../NekoWebShow --browser /path/to/chrome \
  --output build/evidence/cc-new
# 默认矩阵预期返回1：保存失败，不将其计为验收成功。
# --cc-json '0' 可试标量；也可提供结构化 JSON 候选。
python3 tools/reference_capture/probe_mesh_cc.py \
  --generate-only --output build/evidence/cc-inputs
python3 -m unittest discover -s tests/reference_capture -v
```

输出目录必须新建。每个 case 保留输入、命令、日志、返回码、工具/输入/产物 hash；`accepted_semantics` 恒为 false，单纯生成标记 not_captured。`capture.cjs` 在空输出/捕获异常后另存 `.failure.trace.json`，捕获仍返回失败。`trace.js` 在已经复制的 GPU 提交字节中分别计数 NaN、+Inf、−Inf，避免 JSON 把三者都编码成 null 而误判。观察器回归也验证调用者上传后修改缓冲区不改变已记录的提交、bufferSubData 正常更新，以及不读取 shader。

最终冻结目录 `build/evidence/cc-20261014/minimal-final`；摘要与 hash 保存在 [结果](validation/mesh-cc-results.json)。早期探索和 PsBuild 产物保留于同级目录，未将资源加入版本跟踪。

## 验证与下一步

Python 114 个用例：102 通过、12 个 Qt opt-in 用例 skipped；新增4个探针/观察器用例通过。两个 JS 文件 `node --check` 通过。既有 C18 冻结 importer 可以导入非空32分量 cc，但实际 `nekomote_pose` 在 `test/root/tile` 返回失败 `cc deformation is unverified`，没有静默回退或生成近似画面。C19 没有改 C++、资产 schema 或正式帧路径，因此未重复 C++/ASan/性能基准，也没有声称性能改善或 cc 求值完成。独立只读复核核实了15格隔离条件、实际引用顶点、hash和接受状态。

下一步需要先确定可工作的非空 cc 数据布局：优先合法公开导出的模型/格式资料中仅含 cc 的小样本，或有格式依据的新生成布局。仍按端点放置、恒定 BP、固定 coord、端点/hold 对照逐项区分。只有找到有限且可辨识的有效作用后，才能推导公式、建立独立数值预期及未参与推导的参考对照，并修改 model/importer/runtime 的实际类型和计算。保持当前非空 cc 明确拒绝，不将无效候选加入正式播放路径。
