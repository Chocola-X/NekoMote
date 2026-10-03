# 字段语义与证据

源文件可读及 FreeMote 工具的枚举名称，只能支持候选解释。正式求值需受控参考画面/几何验证；未知语义不会在正式加载时静默近似。

| 字段 | 状态 | 当前证据与下一步 |
| --- | --- | --- |
| source.*.texture.pixel | 结构已验证 | `#resource#N` 对应 resx.Resources[N]；需解码图片核对尺寸 |
| motion.parameter / layer.parameterize | 结构已验证，映射候选 | 主模型 parameter 位于 motion，层索引必须保留 null；参考端固定变量采样 |
| frame.content 缺失/null | 结构已验证，行为未验证 | 末尾 type=0 无 content，不能转换成默认可见帧 |
| mesh.bp | 候选 | 主模型 32 个分量；4×4 双三次 Bézier 为待验证解释；不得称为已确认 |
| mesh.cc | 未验证 | 保留任意数量有限分量，不能假定和 bp 一样为 32 |
| meshCombine / meshSyncChildMask | 未验证 | 空白变形层与图像层嵌套；需单变量与组合输入实验 |
| content.mask | 未验证 | 很多非零值对应内容属性存在位，不应直接当成 alpha 遮罩 ID |
| inheritMask / transformOrder | 未验证 | 保留原值并记录参考实验；不能直接全部继承父矩阵 |
| stencilCompositeMaskLayerList | 未验证 | 必须保留图层引用，P4 确认组合与清除 |
| metadata.customPartsOrder | 未验证 | 主模型值为字符串数字，不是解析后的 motion 名称；禁止当作当前部件绘制顺序 |
| variableList.frameList | 结构已验证 | 是标记帧列表；不能据此声称获得运行时默认值 |
| timelineControl | 结构已验证，求值未验证 | 轨道/空帧/easing/diff/loop 单独保存；P5 对照 |
| metadata 物理/控制配置 | 未实现 | 完整 source 审计归档保留数据，当前资产不宣称具备物理能力 |
