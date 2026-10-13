# C16：稳态索引、UV 与渲染状态复用

用户要求继续降低长期运行的 CPU/GPU、内存与显存开销，优先实施收益高的候选。
基线 `958a404`，Codex / GPT-6（精确变体未暴露），2026-10-13。
实现限 runtime geometry、GL/Vulkan renderer、协议注释和回归测试；没有更改资产、
公共 ABI、宿主同步或桌面安装。完整结果、输入/二进制 hash 及原始不利样本见
[机器记录](validation/frame-optimization-results.json)，本地原始证据在
`build/evidence/frame-optimization-20261013/`。

## 选择与实现

首测只复用索引，三个模型的收益较小；随后复用已有 RenderVertex 的 UV，
避免每帧重复执行图集坐标与 Float32 保存边界的计算。不是额外复制一份完整顶点缓存。
完整的有序 `(nx, ny)` 序列决定三角形索引；序列变化时重建索引和全部 UV。
拓扑稳定时，每段还校验不可变 IconRect 地址和 TextureId，资源变化重新计算该段 UV。
网格比例相同不等于资源相同。顶点位置、变形、颜色、深度排序、遮罩和物理继续逐帧求值。
失败不发布帧；重复 build_frame、隐藏/恢复和失败后重新求值不复用失效输出。

新增元数据按编译节点数预分配；本机每节点 24 字节，三个代表模型约 10–20 KiB/实例。
它只随实例寿命存在，没有无界历史缓存或新增 GPU 纹理。预热后的既有零分配测试通过，
其范围仍是测试覆盖的项目 C++ 路径，不包含驱动分配，也不证明整个宿主零分配。

GL 在一次 render 内只在图集 ID 改变时重新查找、绑定和设置 texel 变换；每次调用
重新建立缓存，并保留宿主 rectangle binding 恢复。GL/Vulkan 的 scratch mask 在同一
render 中可复用最后写入的 ID；普通 draw 不改 scratch，其他 ID（包括空遮罩）覆盖后
必须重建。每个 masked draw 仍恢复 group clip/use_mask，不跨帧或不同 view 复用。
覆盖精度、过滤、POT 尺寸、透明绘制顺序、错误检查和 Vulkan 同步均保留。

三个代表模型在 tick 240 的遮罩写入计数仍是 4/6/6：ID 交错，未取得遮罩绘制次数收益。
因此不能把“减少重复遮罩”当成这些模型已有的 GPU 加速。其通用复用路径由合成用例验收。

## 正确性与独立复核

- Release CPU CTest：189/189；最终运行设置真实源素材路径，实际执行 real import，
  不把缺少环境变量时的 WARN/return 当作素材验收。
- GL：15 用例/275 断言；Vulkan：9 用例/348 断言，核心及同步验证通过。
- 无窗口 ASan/UBSan：24 scene 用例/2405 断言，启用泄漏检测；不覆盖 Qt/GPU 的 ASan。
- 隔离 Plasma：GL/VK wallpaper layers、item safety、config reject，4/4；没有安装插件。
- 195 模型的固定 seed 初始 RenderFrame（完整顶点/索引/命令/遮罩）与基线完全相等。
- Chocola date-a、Fraise maid-b/casual-a，tick 0/240、GL/VK，1920×1080、fit、
  BG_07A、展开 UI、seed=1、静音、不保存：24 张截图组成 12 对，RGBA 字节完全一致。
- Codex 独立只读复核缓存失效、线程/所有权、数学语义与新增用例，无阻塞项。

新增用例覆盖同顶点/索引总数的 3×2/2×3 网格互换、相同拓扑不同 atlas left 的资源切换、
隐藏/恢复和连续 build；GPU 覆盖 A/普通 draw/A/空 B/A、零索引、镜像及同 ID 内容变化。
195 初态和三个模型的动态抽样不代表全部型号全动态认证。

## 同条件性能

AMD Ryzen 7 5800H、RTX 3060 Laptop、NVIDIA 575.64.03、KDE/X11、GCC 15.1.1、
Qt 6.9.1，Release/O3/LTO/WERROR。性能期间不运行构建、截图、ASan 或 GPU 验证层；
GPU 工作以 `build/gpu-validation.lock` 串行。二进制在变更前冻结，采样前后 hash 不变。

CPU probe 固定 seed=1、先运行 240 author ticks，预热 120，每轮 10000 个
step+prepare_frame；每模型按 base/optimized/optimized/base 四轮。
包含动画、物理、最终几何与帧构建，不含 JSON 采样、GPU、UI、窗口或呈现。
表中是两轮同版本 P50 的中位数，单位 ms。

| 模型 | 基线 | 优化 | P50 降幅 |
| --- | ---: | ---: | ---: |
| Chocola date-a | 0.25760 | 0.25257 | 2.0% |
| Fraise maid-b | 0.40269 | 0.39106 | 2.9% |
| Fraise casual-a | 0.60619 | 0.57850 | 4.6% |

Chocola 首组 P99 从约 0.299 升至 0.328 ms；反序补采的基线/优化 P99 分别约
0.307/0.304 ms，P50 降幅约 3.4%。所有样本保留，不能宣称尾部帧耗时稳定改善。
Fraise 两型号首组 P95/P99 均降低；本轮改善主要是减少 CPU 的重复工作。

player 为 Chocola date-a、BG_07A、展开 UI、1920×1080、30 FPS、seed=1、静音、不保存，
120 帧预热、300 帧采样；每后端也是四轮 ABBA。
CPU submission 指 advance+prepare+背景/角色/UI 提交的墙钟时间，发生在 swap/wait 前，
不等于 GPU 执行耗时。实际帧率均为 29.758–29.797 FPS。

| 后端 | 提交 P50 基线/优化 ms | 稳态 RSS 基线/优化 MiB | 显存基线/优化 MiB |
| --- | ---: | ---: | ---: |
| GL | 1.47685 / 1.45405 | 167.66–167.71 / 167.53–167.67 | 67 / 67 |
| VK | 1.15628 / 1.13560 | 157.88–157.89 / 157.80–157.97 | 61 / 61 |

player GL 的 P95 中位数为 1.84525→1.85975 ms，P99 为 1.97278→2.03950 ms，
后者增加约 3.4%；这一不利样本同样保留，不能宣称整帧尾部稳定改善。

提交 P50 约降低 1.5%/1.8%。/proc 进程 CPU（100% 为一个核心）GL 基线
5.18–5.36%、优化 5.18–5.20%；VK 基线 5.68–5.70%、优化 5.51–5.53%。
GL 范围重叠，不能承诺相同百分比的实际 CPU/功耗下降。RSS/显存没有稳定下降或明显稳态回退。
稳态 RSS 取启动后 6–12 秒，/proc status 约 20ms、按 PID 查询显存约一秒。

一轮优化 VK 捕获到 179.48 MiB 加载峰值，其余 20ms 样本约 157.9 MiB；改为仅
/proc 的 1ms 采样补查四轮后，基线峰值 179.34–179.54、优化 179.18–179.31 MiB。
细采样在两版均捕获约 46.74 MiB RssAnon 的加载瞬态，与解码像素仍存活的阶段一致。
这支持先前峰值差来自采样遗漏的解释，不把粗采样当作精确峰值或丢弃不利样本。

本轮 player GL swap interval 0、VK IMMEDIATE，与基线同条件；不作为实际 Plasma
功耗或常规 FIFO 性能结论。没有直接测量 GPU duration/整套壁纸功耗，也未认证
8–24h、多屏、其他驱动或 Wayland。

## 复现与后续边界

构建/测试使用 `cmake --build --preset release`、`ctest --test-dir build/release -LE gpu`；
GPU 用例在共享锁下运行。Vulkan 需提供实际可用的核心/同步验证层，不把缺失层计为通过。
真实导入测试将 NEKOMOTE_TEST_REAL_ASSET 指向外部 model_conversion（只读）。
CPU probe 输入为 `{"steps":240,"random_seed":1,"benchmark":10000,"dump_frame":true}`，
调用 `nekomote_playback_probe MODEL.nkm.json INPUT.json OUTPUT.json`，按同版本/反版本交替。
player/capture 的完整 argv、输入 hash 与验证环境见机器记录及 C15 同条件入口。

尚有较大但需要独立契约/测量的候选：共享 FPS 配置与 Plasma 调度联动、G34 消除加载期
重复几何编译，以及按实际 GPU profile 决定颜色中间目标的区域缩减。本轮不把它们标成已实现，
不为降低占用删除动态求值、校验、正确遮罩或宿主同步。
