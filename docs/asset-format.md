# 当前资产格式

当前只支持 **schema 6**。运行时不读取历史 schema 或原 E-mote JSON。旧资产须显式重建，版本不支持时明确拒绝。

```bash
build/release/bin/nekomote_import model.json model.resx.json --output assets/new-model
# 外置 PNG 可重复传 --texture 'tex#000=relative-atlas.png'
```

输出目录必须不存在。导入先写旁边的临时目录，成功后重命名；失败仅清理本次临时文件。资源路径相对于 bundle，不包含本机绝对路径。

| 文件 | 用途 |
| --- | --- |
| `model.nkm.json` | `schema=6` 与规范化强类型模型 |
| `textures/N.png` | 仅引用的 PNG 图集 |
| `report.json` | 结构状态、警告和未支持字段；不是播放认证 |
| `source.json` | 离线调查归档，运行时不依赖 |

加载阶段验证数量、有限数值、引用和路径；解析/图片解码不进入稳态帧路径。`Field<T>` 编码为 `{ "state": 0|1|2, "value": ... }`，表示缺失、null、存在；只有 state=2 带 value。末尾空帧不转为可见默认帧。icon 键为 `texture/icon`，motion 键为 `group/name`。

模型加载逐项转换 part/layer/frame 并释放对应 JSON 节点，不同时保留完整 JSON 树与强类型副本。字段顺序不影响数据；重复 JSON 对象键明确拒绝。schema 和资产内容无需迁移。PNG 保留编码色值与 straight alpha，不做 gamma/ICC 变换；解码直接写入最终 RGBA 缓冲。

## 镜像与参数

`mirror` 和 `mirrored_parameters` 都必须存在。后者保存需反向求值的**完整参数名**，不能重复、为空或引用未声明参数；mirror=false 时须为空。

离线导入将已观察的 `metadata.mirrorControl.variableMatchList` 中 `_LR`、`_slant` 按子串匹配展开为名字列表，其他模式明确拒绝。声明范围包含公共变量、控制器、时间线、物理输出及所有 motion 的参数绑定。运行时不进行模式/字符串匹配。

作者输入、动画状态及公开观测值保持作者方向；几何求值复制最终参数后反向一次，再执行成对 clamp 和参数化。原 PSB 的纯差分/叠加/主动作已分别核对，不按通道猜测方向。物理将 locator 转到呈现模型坐标后积分，输出仍是作者参数。输出呈现仍保留水平镜像。这两步均需要，不通过删除呈现镜像解决方向差异。证据与适用范围见 [C5 交接](c5-mirror-mask-handoff.md)。

## 控制与物理

资产包含 `paired_clamps`、强类型 `variable_controls`、`author_scale`、`spring_controls`、`chain_controls`。物理保存作者系数、三分量快照、锚点与输出绑定；未知非 null 字段保留来源并报告，编译时明确处理或拒绝。结构可读不代表求值已认证。

有物理控制器时 author_scale 必须有限且为正。链条末端使用 `normal_velocity_damping`：1 去除法向速度，2 反射；无旧字段别名。保存 Float32 作者数值，绑定和锚点在编译时解析。物理输出参数即使没有 drawable 使用也声明，空显示帧的已选资源定位点与未选资源区分。

## 一次性 5 → 6 迁移

首选从明确、已核实来源重新导入到新目录。现有会话/背景/音频配置可以保留。需要发布到已有角色目录时：

```bash
python3 tools/rebuild_asset_schema.py --assets assets/characters \
  --rebuilt <逐项核实的新bundle根目录> --backup <不存在的备份目录>
```

这是显式离线迁移工具：全量比较作者数据和解码图集像素，通过后备份旧模型，再逐项发布；失败回滚本次写入。只允许 schema 与新镜像参数字段变化，以及旧格式 documented mirror=false 缺省的显式规范化。纹理差异须先独立核实，不能静默覆盖。冻结历史资产不迁移，输入矩阵只在内容等价证据成立时更新 asset SHA；源 PSB、驱动、输入与参照图片 hash 保持原记录。

## 背景资产

```
assets/backgrounds/
├── background-names.json     # 可选，键为文件名，值含 zh-CN 名称
├── BG_07A.png                # 静态背景（根目录）
├── BG_13A.png
└── dynamic/                  # 动态背景（分时段 A/B/C 三相）
    ├── BG_22A.png            # 06:00–16:00 白天
    ├── BG_22B.png            # 16:00–18:00 黄昏
    ├── BG_22C.png            # 18:00–次日06:00 夜晚
    └── ...
```

静态背景的 `name` 为文件 stem（如 `BG_07A`）。动态背景的 `name` 为组名（如 `BG_22`，不含相后缀），要求 A/B/C 三相齐全才收录。`config.json` 的 `background` 字段存 `name`：静态为 `BG_07A`，动态为 `BG_22`。

`background-names.json` 键为完整文件名（如 `"BG_07A.png"`、`"BG_22A.png"`），动态组用 A 相的名称作为组显示名。文件不存在时回退到 stem。

导入工具：

```bash
python3 tools/import_backgrounds.py --reference-root ../NekoWebShow --dest assets
```
