# P4 遮罩与静态合成子步骤

P4 仍进行中，未认证完整播放。当前已实现四型号初态的完整原生画面、主模型完整可见身体及缩放对照；严格固定姿态矩阵尚有轮廓残差，嵌套/反向遮罩仍明确不支持。

## 经验证的实现

- 编译时在展开的 motion 实例内绑定 stencilType=5 的组合图层引用；实例生成 mask commands，复用正常几何和索引。没有源 JSON 或 GL 句柄进入核心。
- 组合覆盖为 alpha 的 union：`a + b*(1-a)`，每次应用清空。mask 目标按输出宽高分别取向下的二次幂，线性采样；源和绘制包围盒各扩展 2 个输出像素后求交。坐标、宽、高分别向下缩放，匹配参考端裁切。
- 源 icon.attr=1 请求透明颜色保护。RGBA8 直通中间目标先做 RGB `src*(1-dstAlpha)+dstRGB*dstAlpha`、保持 alpha，再做普通 over。最终输出将 RGB 乘以 alpha；alpha ≤0.003 的像素丢弃。普通预乘模式继续用于通用图元。
- 原 WebGL 的 mask 附件实际为 **5/5/5/1 位**；颜色目标为 8/8/8/8 位。在当前 NVIDIA/ANGLE 环境直接查询颜色位数并读回 green 的 32 个等级确认，见 [精度证据](validation/p4-mask-precision.json)。不能从上传的 4444 type 推断附件位数。原生显式分配 RGB5_A1，并核实 green=5；最初使用实际 green=8 的目标导致眼睛残差，已修正。
- canonical identity BP 仍参与网格密度选择，但其坐标映射直接保持不变；避免多项式舍入让 mask 边界跨越整数。局部网格直接从图像尺寸生成，图集位置只用于 UV；更改图集打包不改变模型几何。
- Bézier 基函数与累加使用 double，最终坐标保持 float。只是减少运算舍入，尚未认证原引擎的所有精度边界；未保留实验性的倒数、控制器、系数或分域计算开关。

## 验证与未通过项

当前 [30 组静态图像报告](validation/p4-static-images.json)：23 PASS、7 FAIL。实际原生网格/UV/三角形全部通过既定 0.001px 几何门槛，最大 0.000251655px；图像仍保留严格 2/255 的 alpha 与黑/白合成 RGB 门槛。

四型号默认姿态、闭眼、眼球移动、张嘴、手臂切换、主模型头身组合等通过。7 组失败共 10 个像素，合成 RGB 最大差 3/255、alpha 最大差 1/255；没有放宽阈值或把 FAIL 改为 PASS。失败输入：主模型 body-lr-ud、body-lr30、head-lr-minus1、head-lr-ud、head-ud30，以及 vanilla-date-a/chocola-teenage 的 combination。完整阶段保持未完成。

主模型 1200×2000、scale=0.65、coord=(0,-450) 的完整可见身体通过：alpha 差 0、RGB 最大差 1/255。已查看原生与原 WebGL 并列图；两端素材均到膝部结束。离线叠到原项目 BG_29A 背景后 RGB 最大差 1/255，见 [完整身体报告](validation/p4-fullbody-image.json)。背景测试不代表 application 已实现背景播放。

诊断隔离实验直接使用参考顶点检查后端，得到 alpha/RGB 合成零差；随后分别替换位置和 UV，位置替换复现残差、UV 替换通过。此实验数据及代码只在忽略的 build/evidence，未进入运行时输入。纯 float/double、Bezier power basis、坐标放置及控制器精度实验不能统一闭合所有输入，均撤掉。下一步继续查明几何精度边界及需要的嵌套遮罩，不用某个模型名/姿态选择公式。

Debug/Release 42/42 CTest 入口通过（GPU 入口含 5 个用例）；ASan/UBSan 无显示核心 41/41 通过。GPU 覆盖保护合成、mask union/空 mask/重复应用、resize、坏引用、嵌套拒绝与预热后 100 次零普通 C++ new；核心覆盖作用域隔离、失败后空帧、未知父层/帧拒绝、资源切换、图集打包独立性和 200 次零普通 C++ new。源素材和原仓库保持只读。

## 生命周期与性能

Renderer 不拥有窗口/上下文；所有操作及销毁需在同一个当前上下文、图形线程执行。`reserve(Capacity)` 预留 VBO/EBO、commands/masks 容量；`resize_output` 是显式分配边界，相同尺寸不分配/查询。RGBA8/5-bit scratch 目标通过 RAII 管理；正式帧无截图读回、FBO 创建、纹理上传或逐图元 GL 状态查询。尚未优化 mask 缓存，当前按绘制命令重建。

2026-10-04：Release+LTO，Ryzen 7 5800H、RTX 3060 Laptop、NVIDIA 575.64.03、KDE/X11。主模型固定头身组合、431 nodes/33 sprites、800×1080、120 帧预热/2000 次采样：CPU evaluate+build_frame P50/P95/P99=0.609517/0.664972/0.700243ms。默认姿态 render+swap（hidden、vsync=0）为 0.165947/0.189413/0.388675ms。两份范围不同，不能相加为实际呈现帧时间；未测完整壁纸的功耗、GPU 时长或长期内存。

所需而未知的 mask 源/blank 绘制/嵌套会失败，并发布空帧；未选中的空定义保留诊断。图像通过的范围不能推广到动画、cc、部分继承、物理、全部素材或正式桌面宿主。
