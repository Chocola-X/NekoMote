# 当前资产格式

离线导入命令：

```bash
build/dev/bin/nekomote_import model.json model.resx.json --output build/assets/model
# 有外置 PNG 时，重复传入 --texture 'tex#000=relative-atlas.png'
```

输出目录必须不存在。写入先在本项目指定输出目录旁创建临时目录，成功后重命名；失败只清理本次临时目录。输出只复制引用的图集，不复制全素材。

- `model.nkm.json`：`schema=4` 与强类型模型数据。只支持当前 schema；不支持的版本要求重新导入。
- `textures/N.png`：相对于 bundle 的 PNG 路径。
- `report.json`：结构导入状态、尚未支持的字段及警告。`playback_certified=false`，不是完整模型运行时能力声明。
- `source.json`：完整源模型离线调查归档，不作为运行时输入。后续增加规范化字段时从原素材重新构建。

`Field<T>` 编码为 `{ "state": 0|1|2, "value": ... }`，分别表示缺字段、null、存在值。只有 state=2 带 value；0 值不等同于缺失。frame 的 content 也保留状态，末尾空帧不转成可见默认帧。icon 使用 `texture/icon` 键，motion 引用使用 group/name 组合；图集引用和 motion 引用在加载时校验。

加载期间完成 JSON、数量、数值、引用和资源路径检查。此格式仅用于当前开发阶段，允许在字段语义确认后直接演进；不增加历史格式回退。

Schema 2 增加 `paired_clamps`，保存成对变量控制的类型、变量、范围和启用状态。Schema 1 明确拒绝：用同一导入命令写入新目录重建，不自动回退。

## Schema 3 控制规则

加入 variable_controls：显式 kind（Linear/Immediate/Expression/Eye/Mouth/Selector）、enabled、变量/口型绑定、edge 区间和 junction 拓扑、眨眼参数、选择器 on/off 输出。字段名与类型已规范化，运行时不读 source.json。它们的结构保留不代表全部行为已认证；当前 nonlinear expression routing、selector fade 和自动控制器仍未通过。

当前只支持 schema 4，schema 1/2/3 明确拒绝；用相同 `nekomote_import <源 JSON> <resx> --output <新目录>` 重建。四型号新旧 model 对象除新增 variable_controls 外逐字段相同；当前固定输入 hash 见 docs/validation/static-inputs.json，历史阶段材料不改写。

## Schema 4 物理数据

新增 `author_scale`（保留 Field 状态）、`spring_controls` 与 `chain_controls`。胸部弹簧保存 stiffness、friction、gravity、axis_scale、vertical_offset、三分量 position/velocity/previous_anchor；头发与配件链用显式 Hair/Parts kind 区分，保存两个粒子的 length、rest_position、position、velocity，方向摩擦、伸长系数、速度边界、水平/竖直缩放、竖直效果模式、弯曲参数和保存的 phase/gain。输入数值规范化为 Float32，保持源 z 和快照前锚点，未验证语义不丢弃。

每个控制器的 binding 保存原字段路径、label、anchor、preset、输出变量、enabled 及 `unsupported_fields`。未知控制器/快照/向量字段的非 null 路径同时写入资产和离线报告，后续编译器须明确处理或拒绝。preset 是保存的作者配置名称，不能据名称选择未验证公式。锚点资源作用域和变量 ID 在场景/物理编译时绑定，不在每帧解析字符串。

存在物理控制器时要求 author_scale 为有限正数；必需系数与保存快照不得缺省或 null。校验两个粒子及三分量向量形状、非负系数、正长度、摩擦范围、数值有限和不同输出绑定。`velocity_retention` 不限制为 <=1：真实素材有 `v_bound=2`。这只认证规范化和结构，不认证该边界值的求值公式或完整播放器。

schema 3 及更早资产需用页面开头命令重新导入到新目录；不提供历史读取或隐式迁移。四型号的新旧 model 对象除上述三个新增字段外逐字段相同，迁移与 hash 见 [记录](validation/p6-schema4-migration.json)。当前静态/动态基线更新为 schema 4 资产 hash，原 PSB/驱动/输入/参考图片与历史阶段报告保持原记录。
