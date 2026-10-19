# PNG 无损重压缩

2026-10-19 后续复核：修复原生 CI 漏构建 PNG 工具和包审计清单，并发现异机资产树
混入三份旧名称字典，已恢复并合并资产提交；全量像素及目录结果见
[Ubuntu20 与资产复核](png-ci-assets-audit-2026-10-19.md)。下文保留原交付记录；
逐行贪心滤波是局部候选策略，不能证明全图 DEFLATE 的全局最小编码。

基线为 C20 资产迁移后的 `fd5df9f`。本轮将资产树中全部 324 张 PNG 转为优化编码并
将重编码固化进导入链路；纹理像素、运行时解码路径与内存占用均不变。

## 结论

| 内容 | 数量 | 处理 | 原字节 | 新字节 |
| --- | ---: | --- | ---: | ---: |
| 模型纹理与图标 | 203 | 逐行最优滤波 + zlib level 9 | 660,306,400 | 512,815,921（-22.3%） |
| 背景图 | 121 | 剥离元数据块，重编码仅更小时采用 | 323,676,914 | 289,256,783（-10.6%） |
| PNG 合计 | 324 | — | 983,983,314 | 802,072,704（-18.5%） |
| 全部资产树 | — | — | 978 MiB (du) | 806 MiB (du) |

逐文件字节数见 `assets/png-migration-report.json`。

原 324 张中 322 张使用 zlib 默认级别 6 与混合自适应滤波（上游解包工具的保存默认值），
背景图另嵌 30.02 MiB Adobe XMP（`iTXt`）等元数据块。抽样实测表明背景像素数据已接近
最优（重压缩收益 -2.7%~+4.9%），收益几乎全部来自元数据剥离；模型图集收益 20-31%，
全部来自逐行滤波器选择（级别 6→9 本身贡献接近零）。迁移策略据此分流：背景仅剥离
元数据，完整重编码仅在更小时采用；纹理重编码，不小于源时保留源字节（本次 1 张：
`characters/azuki/winter-b/textures/1.png`）。

## 实现与固化

- `asset_io::recompress_png`：libpng 按原始色彩布局读入（8-bit RGB/RGBA，接受交织），
  每行在 5 种滤波器中取 level 9 实际压缩最小者，单 IDAT 写出非交织容器。发布前
  双路校验：原始布局逐字节回读一致，且 `load_png` 运行时路径的 RGBA 像素与裁切
  region 与源完全一致。
- `asset_io::strip_png_metadata`：逐 chunk 校验 CRC 后仅保留
  IHDR/PLTE/tRNS/IDAT/IEND，像素字节原样复制；`load_png` 校验发布。
- `write_bundle`（`asset_bundle.cpp`）纹理落盘由 `copy_file` 改为 `recompress_png`，
  新导入即优化；`tools/import_backgrounds.py` 新增 `--recompress` 调用 strip 子命令。
- 工具 `nekomote_png_recompress recompress|strip SOURCE DEST` 输出 JSON 字节数；
  迁移脚本 `tools/migrate_png_assets.py` 并行编排（8 进程），来源树只读，临时目录
  staging 后原子发布，逐文件记录报告。

编码侧明确拒绝 16-bit、调色板与 tRNS 源（当前 324 张全为 8-bit RGB/RGBA，无一命中）；
失败不产出目标文件。

## 验证

- 324/324 张在工具内通过双路逐像素校验；迁移报告 0 错误。
- 新树五角色（chocola/date-a、vanilla/casual、azuki/casual、coconut/casual、
  fraise/maid-a）加载 + 120 步求值通过；vanilla 自身反应触发语音命令，
  FLAC 相对链接在新树有效。跨角色 reaction 标签报 `InvalidReaction`（session=2）
  为既有行为，旧树同样失败，非本轮回归。
- CTest 229/229 通过（`LC_ALL=C`，readelf 本地化问题见既有记录）；
  单元测试新增 3 例：重压缩无损且不增大、不支持布局与缺源文件拒绝、
  元数据剥离正/负例（含损坏 CRC 拒绝）。
- 全量迁移 8 进程约 9 分钟（Xeon E5-1680 v4）。

## 范围与未覆盖

- 只影响磁盘占用与分发包大小；解码后 RGBA、GPU 纹理容量、稳态 RSS 与功耗不变，
  不宣称内存收益。逐行裁切（`TextureRegion`）在双路校验中显式比对。
- zopfli 可再省约 1-3%，但需新增依赖，按依赖约束未采用；当前为 zlib 约束下的
  最优编码（逐行滤波 + level 9）。
- 磁盘数字为 `du -sm` 与逐文件字节统计，未区分文件系统块开销；长期运行、多屏与
  其他驱动不在本轮范围。原树完整备份于
  `build/evidence/png-recompression-20261019/assets-before/`。
