# 当前资产格式

离线导入命令：

```bash
build/dev/bin/nekomote_import model.json model.resx.json --output build/assets/model
# 有外置 PNG 时，重复传入 --texture 'tex#000=relative-atlas.png'
```

输出目录必须不存在。写入先在本项目指定输出目录旁创建临时目录，成功后重命名；失败只清理本次临时目录。输出只复制引用的图集，不复制全素材。

- `model.nkm.json`：`schema=3` 与强类型模型数据。只支持当前 schema；不支持的版本要求重新导入。
- `textures/N.png`：相对于 bundle 的 PNG 路径。
- `report.json`：结构导入状态、尚未支持的字段及警告。`playback_certified=false`，不是完整模型运行时能力声明。
- `source.json`：完整源模型离线调查归档，不作为运行时输入。后续增加规范化字段时从原素材重新构建。

`Field<T>` 编码为 `{ "state": 0|1|2, "value": ... }`，分别表示缺字段、null、存在值。只有 state=2 带 value；0 值不等同于缺失。frame 的 content 也保留状态，末尾空帧不转成可见默认帧。icon 使用 `texture/icon` 键，motion 引用使用 group/name 组合；图集引用和 motion 引用在加载时校验。

加载期间完成 JSON、数量、数值、引用和资源路径检查。此格式仅用于当前开发阶段，允许在字段语义确认后直接演进；不增加历史格式回退。

Schema 2 增加 `paired_clamps`，保存成对变量控制的类型、变量、范围和启用状态。Schema 1 明确拒绝：用同一导入命令写入新目录重建，不自动回退。

## Schema 3 控制规则

加入 variable_controls：显式 kind（Linear/Immediate/Expression/Eye/Mouth/Selector）、enabled、变量/口型绑定、edge 区间和 junction 拓扑、眨眼参数、选择器 on/off 输出。字段名与类型已规范化，运行时不读 source.json。它们的结构保留不代表全部行为已认证；当前 nonlinear expression routing、selector fade 和自动控制器仍未通过。

只支持 schema 3，schema 1/2 明确拒绝；用相同 `nekomote_import <源 JSON> <resx> --output <新目录>` 重建。四型号新旧 model 对象除新增 variable_controls 外逐字段相同；当前固定输入 hash 见 docs/validation/static-inputs.json，历史阶段材料不改写。
