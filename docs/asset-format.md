# 当前资产格式

当前只支持 **schema 6** 的 `model.nkm` 二进制压缩容器。运行时不读取旧的
`model.nkm.json` 或原 E-mote JSON；旧资产须显式转换或从 FreeMote 解包输入重建。

```bash
build/release/bin/nekomote_import model.json model.resx.json --output assets/new-model
# 外置 PNG 可重复传 --texture 'tex#000=relative-atlas.png'
```

输出目录必须不存在。导入先写旁边的临时目录，成功后重命名；失败仅清理本次临时文件。资源路径相对于 bundle，不包含本机绝对路径。

| 文件 | 用途 |
| --- | --- |
| `model.nkm` | `schema=6` 与规范化模型的 MessagePack + zlib 压缩容器 |
| `textures/N.png` | 仅引用的 PNG 图集 |
| `report.json` | 结构状态、警告和未支持字段；不是播放认证 |
| `source.json` | 离线调查归档，运行时不依赖 |

加载阶段验证数量、有限数值、引用和路径；解析/图片解码不进入稳态帧路径。`Field<T>` 编码为 `{ "state": 0|1|2, "value": ... }`，表示缺失、null、存在；只有 state=2 带 value。末尾空帧不转为可见默认帧。icon 键为 `texture/icon`，motion 键为 `group/name`。

容器头为 8 字节 `NKMMPK01`，随后是 uint64 小端编码的未压缩 MessagePack 长度，
余下为 zlib 数据流。内部保留 schema 6 的字段和值语义；更换容器无需改变模型求值公式。
PNG 保留编码色值与 straight alpha，不做 gamma/ICC 变换；解码直接写入最终 RGBA 缓冲。
资产树中的 PNG 由导入与迁移工具统一编码：模型纹理与图标为逐行最优滤波 + zlib level 9
的无损重编码，背景图剥离全部辅助元数据块（XMP/iCCP/gAMA 等），像素字节保持原样。
两种编码都在发布前解码回读并逐字节比较；`nekomote_png_recompress recompress|strip`
提供同样的两个子命令。优化只影响磁盘占用，解码后显存与稳态内存不变。

模型 PNG 在加载边界按非零 RGBA 包围盒裁切，并保留一像素过滤边距；alpha=0 的 RGB 也参与包围盒，避免改变 straight-alpha 线性过滤。原始 texture/icon 尺寸、原点、几何和帧 UV 不变，`TextureRegion` 记录原图尺寸及裁切偏移；GL/Vulkan 后端在像素坐标中采样裁切纹理。普通非交错 PNG 从同一打开的文件按行扫描后再解码，仅保留裁切区域和一行工作缓冲；Adam7 合并完整行后裁切。全零纹理存为 1×1。PNG 文件及裁切坐标约定保持不变；模型容器需要显式转换。具体收益与验收见 [纹理裁切优化](texture-cropping-2026-10-12.md)。

## 镜像与参数

`mirror` 和 `mirrored_parameters` 都必须存在。后者保存需反向求值的**完整参数名**，不能重复、为空或引用未声明参数；mirror=false 时须为空。

离线导入将已观察的 `metadata.mirrorControl.variableMatchList` 中 `_LR`、`_slant` 按子串匹配展开为名字列表，其他模式明确拒绝。声明范围包含公共变量、控制器、时间线、物理输出及所有 motion 的参数绑定。运行时不进行模式/字符串匹配。

作者输入、动画状态及公开观测值保持作者方向；几何求值复制最终参数后反向一次，再执行成对 clamp 和参数化。原 PSB 的纯差分/叠加/主动作已分别核对，不按通道猜测方向。物理将 locator 转到呈现模型坐标后积分，输出仍是作者参数。输出呈现仍保留水平镜像。这两步均需要，不通过删除呈现镜像解决方向差异。证据与适用范围见 C5 交接（已归档）。

## 控制与物理

资产包含 `paired_clamps`、强类型 `variable_controls`、`author_scale`、`spring_controls`、`chain_controls`。物理保存作者系数、三分量快照、锚点与输出绑定；未知非 null 字段保留来源并报告，编译时明确处理或拒绝。结构可读不代表求值已认证。

有物理控制器时 author_scale 必须有限且为正。链条末端使用 `normal_velocity_damping`：1 去除法向速度，2 反射；无旧字段别名。保存 Float32 作者数值，绑定和锚点在编译时解析。物理输出参数即使没有 drawable 使用也声明，空显示帧的已选资源定位点与未选资源区分。

## 现有资产一次性迁移

只接受当前 schema 6 的来源 JSON；更早模型须从 FreeMote 解包数据重新导入。
两个命令都生成完整的新资产树，原树保持原状，目标必须不存在：

```bash
python3 tools/migrate_audio_assets.py --source assets --output assets-audio \
  --converter build/release/bin/nekomote_audio_convert
python3 tools/migrate_model_assets.py --source assets-audio --output assets-new \
  --converter build/release/bin/nekomote_model_convert
```

音频按原声音 namespace 分组；模型转换后解码比较全部字段及类型，再移除新树中的旧 JSON。
迁移验证来源树未发生修改，PNG、来源归档和其他配置保留原字节；失败不发布残缺目录，
发布不覆盖已有目标。确认新树可加载后，再保留原树作为备份并将新树移到正式位置。
冻结的历史证据不改写；旧容器不会自动回退加载。

现有资产 PNG 编码优化的一次性迁移：

```bash
python3 tools/migrate_png_assets.py --source assets --output assets-png \
  --tool build/release/bin/nekomote_png_recompress
```

纹理与图标重编码为逐行最优滤波 + zlib level 9；背景先剥离元数据块，仅当完整重编码
更小时才采用。重编码不小于源的文件保留源字节。新导入的模型纹理由 `nekomote_import`
自动重编码，不再原样复制。

## 角色音频

```text
characters/chocola/
├── audio/
│   ├── main/voice.flac
│   └── little/voice.flac
├── date-a/audio -> ../audio/main
└── koneko/audio -> ../audio/little
```

`main` 对应原角色声音 namespace，`little` 对应 `ko<角色>`。分组依据原数据，
不按衣服名推断（teenage 也可能使用小猫语音）。同角色内重复文件使用相对文件链接。
会话仍以 `audio/voice.flac` 引用，因此没有每帧或播放时的目录回落搜索。
加载阶段解析并检查链接必须落在本角色内；移动整棵资产树后相对链接仍有效。
型号若需要独有语音，可使用自己的 `audio/` 目录或指向角色下独立语音组的相对链接。

运行时通过 libFLAC 一次解码到最终 float PCM 和 RMS；设备线程复用该数据。
FLAC 缩小磁盘占用，稳态 PCM 大小与原 WAV 相同。FreeMote 的整数 PCM WAV
由离线转换器无损编码并验证；float WAV 明确拒绝，不通过量化冒充无损。

## 背景资产

```
assets/backgrounds/
├── background.json     # 可选，键为文件名，值含 zh-CN 名称
├── BG_07A.png                # 静态背景（根目录）
├── BG_13A.png
└── dynamic/
    └── BG_22/
        ├── config.json       # phases/transition_seconds
        ├── A.png
        ├── B.png
        └── C.png
```

静态背景的 `name` 为文件 stem（如 `BG_07A`）。动态背景的 `name` 为组名（如 `BG_22`，不含相后缀），要求 A/B/C 三相齐全才收录。`config.json` 的 `background` 字段存 `name`：静态为 `BG_07A`，动态为 `BG_22`。

`background.json` 键为完整文件名（如 `"BG_07A.png"`、`"BG_22A.png"`），动态组在 `dynamic/background.json` 中以组名为键保存显示名。文件不存在时回退到 stem。

导入工具：

```bash
python3 tools/import_backgrounds.py --reference-root ../NekoWebShow --dest assets \
  --recompress build/release/bin/nekomote_png_recompress
```

`--recompress` 传入工具路径时按迁移同策略优化：先剥离元数据块，再尝试逐行最优
滤波重压缩，仅在结果更小时采用；负收益保持剥离后的原样。不传则原样复制。
