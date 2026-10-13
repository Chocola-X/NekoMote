# C15：模型图集裁切与像素缓冲释放

任务来自用户要求降低内存/显存，并询问扫描是否可在异步加载中完成。
基线 `a17b5ad`；执行日期 2026-10-12–13，Codex / GPT-6（精确变体未暴露）。
本轮改动覆盖 asset_io PNG 解码、Controller 候选、GL/Vulkan 纹理上传与采样、
player/preview/ABI/截图工具调用者及契约测试；不改外部素材、模型 schema、C ABI 或桌面配置。

## 裁切与画面契约

模型加载时找出非零 RGBA 的最小外接矩形，保留一圈全零过滤边缘，并保留原图边界
的 CLAMP_TO_EDGE 行为。alpha=1、alpha=0 但 RGB 非零的像素均保留；只按 alpha 裁切
会改变 straight-alpha 双线性采样。全零图集使用 1×1 零纹理；内部透明洞仍保留。
当前渲染没有 mipmap 或各向异性采样，单 texel 的过滤边缘对应这一实际契约。

模型原始 UV、图块位置、网格、变形和遮罩不改。后端按原始尺寸与裁切偏移转换为
上传图集的 texel 坐标：GL 使用 sampler2DRect，VK atlas 使用 unnormalized sampler
并以 textureLod(..., 0) 采样；背景及完整纹理使用完整尺寸的同一转换。
遮罩/中间颜色/UI 使用其既有采样方式。GL 在每个上传/绘制边界恢复宿主的 unit 0
rectangle binding 与 active texture；新增绑定不依赖 Qt 的 2D 状态重置。

曾尝试归一化 UV 换算，真实图像出现少数 2/255 误差，超过既定 1/255 门槛，
因此未采用。最终 texel 坐标路径在本机三型号、两个时刻、两个后端的完整画面中
逐字节一致；这不代表其他驱动或 195 型号全部动态画面的认证。
采样约束依据 [Vulkan 官方 sampler 规范](https://docs.vulkan.org/refpages/latest/refpages/source/VkSamplerCreateInfo.html)
与 [OpenGL 3.3 官方规范](https://registry.khronos.org/OpenGL/specs/gl/glspec33.core.pdf)。

## 内存和异步所有权

非 Adam7 PNG 第一遍按行解码并扫描边界，第二遍只保存裁切矩形；峰值像素存储为
裁切大小加一个原图行，不同时保留完整原图与裁图。两遍共用一个 FILE 防止原子替换
造成旧边界配新图；不支持外部同时原地覆写正在读取的素材。Adam7 需要先合并完整图，
再裁切；现有 202 张模型图集全部非 Adam7。PNG 色值归一化和错误校验保留。

实测普通 vector 版本虽然已释放像素，glibc 仍多保留约 8 MiB arena 空闲页：
诊断快照中存活堆约 7.2 MB 基本相同，arena 空闲量由 14.2 MB 增至 22.6 MB。
最终解码像素由 asset_io 的无状态映射分配策略管理；DecodedImageRgba8 移动拥有
缓冲，销毁时 munmap，不调整全局 malloc 策略、不调用 malloc_trim。
模型、背景和图标的 PNG 解码都使用这一所有权；ImageViewRgba8 仅供同步上传/导出借用。
低层 C ABI 上传可直接借用经校验的输入，免去额外整图 CPU 复制。

模型切换的 load_candidate_cpu 已通过 std::async 在工作线程执行；扫描和裁切处于
同一 CPU 候选加载函数，没有增加帧内扫描或改变线程归属。future 只交接拥有的 CPU
数据，poll_candidate 使用 wait_for(0)，GPU 上传、prepare、resize、成功提交和失败回滚
仍由求值/图形线程执行。首载仍同步；不能承诺所有切换完全无停顿。

## 素材规模

195 个模型的 202 个纹理引用与 PNG 原始尺寸一致。202 张真实图集逐张经过实际
C++ 加载器裁切，再在被裁边缘补零复原，与完整解码逐字节比较，全部一致。
199 张可裁切，总 RGBA 字节量由 6,296,633,344 降为 4,489,676,856，减少 28.70%。
这是素材集合的像素存储汇总，不是同时加载全部素材的进程内存预算。

| 图集 | 原 RGBA MiB | 裁后 MiB | 上传尺寸 |
| --- | ---: | ---: | --- |
| Chocola date-a | 32 | 24.64 | 4096×1577 |
| Fraise maid-b | 32 | 25.44 | 4096×1628 |
| Fraise casual-a | 32 | 18.61 | 4096×1191 |

## 验证与测量

AMD Ryzen 7 5800H、RTX 3060 Laptop、NVIDIA 575.64.03、KDE/X11、GCC 15.1.1，
Release/O3/LTO/WERROR，libpng 1.6.50、Qt 6.9.1。内存表单位 MiB；采样使用默认 glibc，
不附加 ASan、截图、验证层或替代分配器；GPU 工作串行，并冻结测量二进制。

player 为 Chocola date-a、BG_07A、1920×1080、展开 UI、seed=1、静音、不保存配置，
30 FPS、120 帧预热后采样 300 帧。每后端按 base/crop/crop/base 各两个独立进程，
/proc status 每约 20ms、按 PID nvidia-smi 每约一秒，稳态取进程启动后 6–12 秒。

| player 后端 | 原稳态 RSS | 裁后稳态 RSS | 原/裁后显存 |
| --- | ---: | ---: | ---: |
| GL | 152.6–152.9 | 152.8–154.7 | 73 / 67 |
| VK | 143.0 | 143.0–143.1 | 67 / 61 |

模型 CPU 像素上传后本来就释放，因此不能把图集减小比例直接当作稳态 RAM 的下降比例。
最终单模型稳态没有稳定的约 8 MiB 回退；GL 的一轮约 2 MiB 波动也保留在记录中。
单模型采样峰值 GL 原 157–165 / 新 161–162，VK 原 162–194 / 新 179，
不足以宣称单模型进程峰值总会降低。完整 atlas 像素缓冲本身则按前表缩小。
本批 RSS 比 C14 测量低约 15 MiB，包含文件/驱动映射波动；仅比较本批交错进程。
实测呈现节奏 29.740–29.758 FPS；GL CPU P50 原 1.474–1.478 / 新 1.451–1.481 ms，
VK 原 1.133–1.146 / 新 1.135–1.148 ms，没有据此宣布 CPU 提速。
该入口包含 advance、prepare、角色/背景/UI 执行，到 swap 前的 CPU 时间；
不包含 GPU 完成时间和 swap/wait。VK 入口使用 IMMEDIATE 呈现，不替代 Plasma/FIFO 认证。

高层 ABI 宿主另验证 18 次真实异步切换：初态 Fraise casual-a、同一背景/共享 UI、
1920×1080、seed=1、静音、不保存，六型号序列重复三轮，每次等待候选和 650ms 渐变，
末尾预热 120 帧。GL 为隐藏 EGL 窗口，VK 为离屏 submit/fence；不是 Plasma 进程预算。

| 已预热驱动缓存的 ABI 宿主 | 原切换后 RSS | 裁后 RSS | 原采样峰值 | 裁后峰值 |
| --- | ---: | ---: | ---: | ---: |
| GL | 179.1–183.2 | 167.1 | 215.1–219.2 | 199.8 |
| VK | 164.7 | 149.0 | 229.0–229.2 | 200.2 |

所有切换均成功。GL 匿名页原约 51.5–51.7 / 新 39.7 MiB，VK 原约 50.2 / 新 34.4 MiB。
首次新 EGL 着色器路径的进程 RSS 为初态 211.4 / 切换后 227.8 / 峰值 257.2 MiB；
首次新 VK 路径为 163.1 / 174.9 / 227.8。第二次反序测量的初态恢复到基线约 153/137 MiB，
且 GL device 阶段的额外约 14 MiB malloc 映射消失，支持首次驱动着色器编译/缓存解释。
这是从快照与重复进程推断，未对闭源驱动作调用栈归因；首次开销也不从证据中删除。

加载成本：三图集各三轮交错、每轮连续十次解码，完整 PNG 约 73–77 ms，
扫描+裁切约 143–149 ms，增加约 70ms/图集。此为本机空闲 Release 解码工具的均值，
不是每帧开销或完整切换延迟。模型切换的额外工作在原有 worker，首载仍承担该耗时。

最终验收：

- Release 非 GPU CTest 188/188；新增三类裁切用例及映射移动/归还 OS 用例。
- GL 14 用例/268 断言；VK 8 用例/328 断言，core+同步验证开启；
  覆盖偏移、原边界、透明 RGB、极低 alpha、UV 越界、混合和 raw/crop 遮罩组合，
  有效设备上的非法裁切区域拒绝，GL rectangle 宿主绑定恢复与预热后零 C++ new。
  不将后端局部分配覆盖推定为驱动或整个应用零分配。
- ASan/UBSan 图片与 Controller 生命周期 28 用例/6202 断言、泄漏检测开启；
  其中轮询计数影响断言总数，不作为独立功能数。
- 三型号 × 初态/240 固定 1/60 秒步长 × GL/VK，24 次完整角色/背景/UI 捕获，
  12 对旧/新比较全部 max RGBA=0。VK 捕获强制 core+同步层，错误数为零。
- Plasma 四个隔离入口通过：GL/VK 角色/背景/UI 与正常生命周期、item safety、配置拒绝。
  没有安装到用户桌面或做真实 miku/多屏/长期/跨驱动/195 全动态认证。
- 另一 Codex 执行者独立只读复核 PNG、mmap/借用寿命、线程、GL 状态与 VK sampler/
  push constants；未发现阻止交付的新增缺陷。该复核本身不计测试或 GPU 通过。

早期归一化候选的图像门槛失败、并发构建下 Controller ASan 等待超时、相对验证层路径
导致 Plasma 拒绝启动，均保留；最终空闲检查及绝对层路径复测通过。没有放宽图像门槛。
原始证据在 ignored `build/evidence/texture-crop-20261012/`；
[机器记录](validation/texture-cropping-results.json) 包含输入/二进制/hash/argv/测试与各轮结果。
