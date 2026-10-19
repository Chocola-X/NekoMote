# 换装时复用 GL program 与 Vulkan pipeline

基线 `ae17f20`，Codex / GPT-6（精确变体未知）。用户要求仅落实源码中实际存在、
有收益依据的优化。前期[显存调查](player-switch-memory-2026-10-19.md)中的回收现象
与本轮资源复用分别验收，不把后者描述成驱动保留显存的通用修复。

## 确认的问题与实现

原来每次候选模型都创建全新 renderer，即使设备和 shader 完全相同：
GL 重复加载入口、编译链接 2 个 program、查询 uniform；VK 重复创建 6 条 pipeline、
3 个 renderpass、descriptor/pipeline layouts、samplers 与 4 个 shader modules。
背景 renderer 也重复创建相同资源。这些确实发生在当前项目的正常换装路径中。

两种后端新增 `Renderer::create_peer()`，返回空的、独立模型资源的 renderer，
显式共享源 renderer 的固定绘制资源。首次 renderer 初始化一次资源组；后续模型候选、
渐变旧模型和背景只持有同组引用。player 与高层壁纸 ABI 均接入；Plasma/layer-shell
经高层 ABI 自动使用此实现，没有在桌面壳中复制策略。

共享范围与寿命：

- GL：program、uniform locations 与固定 API 能力。动态 uniforms 每次 render/逐纹理
  重新写入，UV 缓存仍为单次 render 的局部状态；共享不意味着共享模型帧状态。
- VK：pipeline、layouts、renderpasses、samplers、shader modules 与固定设备信息。
- 图集、顶点/索引缓冲、命令、descriptor pool/set、上传池、dummy image、
  composition/mask 中间目标仍归各 renderer。候选失败和新旧模型渐变继续相互隔离。
- 同 context/device、同串行图形线程；源 renderer 可先销毁，最后一个 peer 必须在
  有效 context/device 下销毁。私有资源先释放，再销毁共享包；上下文重建清空全部 peers。

没有全局缓存、跨 context/device 句柄共享、最大模型驻留池、每帧 shared_ptr 复制，
没有新增依赖，也没有新增强制完成等待、定时 trim 或进程级 allocator 策略。
中间目标暂不共享：VK 还需要跨 renderer 统一 image layout、descriptor 引用及 resize
事务，这部分不在本轮已证明的最小收益范围内。

## 测量方法

Xeon E5-1680 v4、RTX 3080 Ti、NVIDIA 580.82.09、KDE/X11、Linux 6.6.107、
glibc 2.42；Release/O3/LTO/Werror。GPU 工作用同一锁串行。

冻结基线源码、全部静态库及原版 player 二进制；两版使用相同私有诊断入口与 API
计数器。正常可见窗口、1920×1080、目标 60 FPS、有声音和 UI、背景 `BG_22A`，
同一套压缩 PNG/model.nkm/FLAC 资产，禁止保存配置，没有系统键鼠注入。

每版每后端均从 `chocola/casual-a-no-bell` 开始，按 dress-a、maid-b-with-bell、
winter-b、swimsuit-a、koneko-a、casual-a-with-bell、date-a 的顺序重复两轮，
再返回初始模型，共 15 次切换，每次约 5 秒，最后保持约 20 秒。
窗口记录确认 visible=1、iconified=0、framebuffer=1920×1080。
该对照不启用 capture、measure-frames、check-window、完成等待或堆回收。

初始化耗时包含 renderer 构造，不包含资源 reserve/upload；GPU 准备包含构造、reserve
与图集上传，不包含 CPU 解码、音频初始化和完整异步切换。排除进程首次冷启动后，
取 15 次加载的中位数。创建次数包含 UI 的独立资源，UI 部分不变。

## 实测结果

下表创建数为进程累计数，包含未改动的 UI 资源；峰值是每秒采样中观察到的最大值。

| 后端/指标 | 基线 | 复用后 |
| --- | ---: | ---: |
| GL：累计 program 创建数 | 38 | 6 |
| GL：稳态/采样峰值 live program | 8 / 10 | 6 / 6 |
| GL：换装 renderer 初始化中位数 | 1.525 ms | 0.023 ms |
| GL：GPU 准备中位数 | 8.956 ms | 8.997 ms |
| VK：累计 pipeline 创建数 | 106 | 10 |
| VK：稳态/采样峰值 live pipeline | 16 / 22 | 10 / 10 |
| VK：换装 renderer 初始化中位数 | 4.297 ms | 4.950 ms |
| VK：GPU 准备中位数 | 44.898 ms | 49.050 ms |

可确认的是重复固定 GPU 对象创建被消除，GL 构造阶段明显缩短。GL 的整体 GPU 准备
没有改善；VK 这一轮构造和准备时间反而较高，不声称加载加速。VK peer 仍创建独立
上传池、dummy image 并完成其上传，时间包含原有队列同步与调度，不能把管线创建
减少等同于整段耗时下降。该次序固定、每版 15 次的样本不足以判断数毫秒差异的归因。

| 后端/版本 | 冷启动 VRAM → 换装后同模型 | RSS（同两阶段） |
| --- | ---: | ---: |
| GL 基线 | 71.254 → 109.254 MiB | 280.152 → 311.234 MiB |
| GL 复用后 | 71.254 → 109.254 MiB | 218.793 → 252.254 MiB |
| VK 基线 | 64.473 → 64.473 MiB | 172.684 → 193.824 MiB |
| VK 复用后 | 64.473 → 64.473 MiB | 146.910 → 165.133 MiB |

这些阶段取末尾五个有效稳定采样的中位数。**没有测得稳态 VRAM 降幅，GL 切换后的
有界驻留仍存在。** 两版返回目标后 GL 图集/中间目标与几何缓冲字节相同；VK 的显式
内存均为 14 次在用分配、60,633,928 字节。驱动内部 program/pipeline 存储不在这些
显式字节统计内。RSS 原始值保留，但跨进程驱动/编译器驻留存在明显差异，前轮调查
已记录，不将这里的冷启动 RSS 差值宣称为本次节省。

另用无诊断插桩的两版 player 分别预热 120 帧、测量 600 帧，正常可见窗口和同一目标
模型。该入口使用 `--measure-frames`，会改变呈现策略，独立于上述正常播放的资源
测量。下表只作帧路径回归检查，CPU 包含 advance、prepare 和提交/命令录制，不含
GPU 执行时间；没有测量功耗或建立 CPU/GPU 利用率降幅。

| 后端/版本 | CPU P50 / P95 / P99（ms） | 帧循环 FPS |
| --- | ---: | ---: |
| GL 基线 | 2.207 / 2.475 / 2.859 | 59.635 |
| GL 复用后 | 2.160 / 2.422 / 2.692 | 59.658 |
| VK 基线 | 1.721 / 1.837 / 2.053 | 59.632 |
| VK 复用后 | 1.718 / 1.844 / 2.165 | 59.628 |

各一轮短样本不证明稳态提速；没有观察到 P50 或实际帧循环的明显回退，VK P99
保留原始波动，不将其隐藏在平均值中。

## 正确性与范围

- Release/LTO/Werror 构建；完整 GL、GLES、Vulkan GPU CTest 三个 runner 通过。
- 重新链接受影响的 ABI 检查程序，非 GPU、非 integration 的 CTest 210/210 通过；
  `clang-format --dry-run --Werror` 和 `git diff --check` 通过。
- Vulkan peer 用现有隔离 Khronos validation layer 复测，loader 日志确认实际加载，
  启用同步验证，无验证错误，28 项断言通过。
- 新增 GL/GLES peer 测试和 Vulkan/GL 对照测试：不同纹理尺寸、相同语义纹理 ID、
  不同 view/flip/alpha、ProtectedStraight/PremultipliedOver、不同输出尺寸的 A-B-A
  画面一致；失败候选不会改变当前画面，源 renderer 提前销毁后 peer 仍可正常绘制。
- 原有遮罩、透明顺序、混合、背景回收、UI、ABI 失败/重建及预热零 C++ 堆分配检查通过。
- 独立只读架构复核未发现阻断问题，重点检查共享包释放顺序、uniform 重设、VK 布局
  相容性、失败事务和上下文重建；复核者没有并行运行 GPU 测试。

本轮不承诺 steady GPU/RAM 降幅，也不宣称解决驱动内部保留空间。初始化阶段去除了
重复工作，整体加载仍受解码、上传、同步、音频等影响。未安装桌面插件、未修改资产，
未覆盖其他驱动、全部角色动态、跨进程/多屏共享和数小时运行。

原始证据、冻结库、诊断源码/构建脚本、完整序列和逐秒记录位于 ignored
`build/evidence/renderer-sharing-20261019/`；汇总与 hash 见
[机器记录](validation/renderer-resource-sharing-2026-10-19.json)。
