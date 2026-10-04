# P3 原生固定姿态场景进展

P3 仍进行中。本文记录 P3 提交时的场景编译、实例独立参数、父子 Bézier 求值与真实网格生成；后续遮罩/颜色保护与当前图像结果见 [P4 静态合成记录](p4-static-composition.md)。下文 FAIL 是当时的历史基线，保留以便追踪实际修复。

nekomote_runtime 只依赖 model 和标准库。加载时展开 motion 引用、检测循环、解析变量/纹理 ID、绑定参数和分配容量。GeometryScene 自持资源/关键帧，不借用加载器的 ModelAsset；GeometryInstance 借用 Scene，并独立持有输入、工作变量、求值状态及帧缓冲。Scene 必须比实例活得更久；返回 span 在下次求值/构帧时失效。求值和构帧单线程、固定 time=0，无 JSON、文件或图形依赖。

已验证资源切换保留左关键帧、type=2 保持、type=3 线性插值；参数 clamp、同域 combine 和跨域变形使用前一步验证的数值内核。零变量作为新实例初态，与四个参考模型一致。尚未认证任意默认变量规则或动态时间线。非零 discretization 的分数输入、cc、zoom/origin/flip、非正常颜色或混合、未知 transformOrder/继承等必需语义明确失败。

网格：非零 D 使用 nx=max(2,floor(D*w/(w+h))+1)、ny=max(2,D-floor(D*w/(w+h))+1) 段。priority=2 的 D=0 采用当前有 BP 的父域最大 D/(domain.width+domain.height) 密度，乘图像宽高和并截断，不在总 division 上强制最小值；细分路径的两轴分别至少两段。null BP 不启用细分，显式 identity BP 会启用。原 PSB 不同域验证加尺寸 fixture 边界区分了“只看图片尺寸”的错误规则；426×20/426 得到 D=20，固定 3/64 密度会错误得到 19。

30 组姿态、四个型号的**实际原生顶点、UV 集合和三角形连接**通过原 WebGL 对照，无缺失/多余顶点或三角形，最大误差 0.000637226px，阈值 0.001px。证据 [p3-native-geometry.json](validation/p3-native-geometry.json)、[网格边界](validation/p3-grid-boundaries.json)。参考 trace 只在诊断 CLI 中请求采样并核对输出，预览不读取 trace。

~~~sh
build/dev/bin/nekomote_preview --geometry-diagnostic \
  --asset build/evidence/chocola-date-a-v2/model.nkm.json \
  --variable head_LR 15 --variable head_UD -20 \
  --origin 400 940 --width 800 --height 1080 \
  --hidden --screenshot build/evidence/native-scene.png
~~~

对应网页 coord=(0,400)，原生 origin=coord+(width/2,height/2)。需要先按当前 schema 导入资产。

初始复杂模型截图已目视检查并与原网页逐像素比较。alpha 完全相同，但 RGB 最大差 144/255、10485 像素超过 2/255；眼睛下缘缺遮罩，透明轮廓颜色存在合成差异。完整图像明确 **FAIL**，记录 [p3-native-neutral-image.json](validation/p3-native-neutral-image.json)。头部组合姿态也已截图并目视核查：alpha 最大误差 1/255，RGB 最大误差 110/255，仍 FAIL，见 [变形图像](validation/p3-native-deformed-image.json)。不以几何通过宣称静态播放闭合。

Debug/Release 39 个 CTest 入口（GPU 入口含 3 用例）、ASan/UBSan 38 个核心入口通过。普通 C++ new 计数：独立实例循环切换资源，预热 120 次后 200 次 evaluate/build_frame 零分配；这不是 GPU/驱动分配结论。

本次发布构建基线：GCC 15.1.1、Release/LTO、AMD Ryzen 7 5800H、KDE/X11、RTX 3060 Laptop / 575.64.03；chocola-date-a 固定 head_LR=15/head_UD=-20/head_slant=10/body_LR=-15/body_UD=20，431 节点、33 图元、4224 顶点。预热 120，连续 2000 次 **evaluate+build_frame** CPU 毫秒 P50=0.628749、P95=0.655709、P99=0.719409。不含 GPU/呈现、窗口、截图、功耗或帧率调度，不能与完整壁纸方案比较。源码基点 4195cd0 加本次场景改动；完整日志保留 build/evidence/p3-scene-benchmark.log。

下一步：用原驱动 GL 状态确认 stencil 作用域/嵌套与颜色保护运算，建立后端无源字段的遮罩协议，再做复杂模型固定姿态、完整身体、缩放和背景图像对照。未支持字段保持显式诊断，不能进入正式加载作为近似。

最新 Fraise 子步骤确认了 meshSyncChildMask=0/1/8 的不同传播方式、细小/细长图像的最小两轴细分，以及透明辅助遮罩不参与颜色绘制但仍贡献 alpha。四个固定姿态的全部实际顶点/UV/三角形通过，三张图像通过、纵向转头剩一个耳尖像素 FAIL。静态诊断与完整播放均在 CPU 放置和缩放后求曲面，最终 View 只做画布中心投影；pose probe 使用冻结参照的显式位置/比例作为输入。详细边界、字段与完整动画 FAIL 见 [子同步证据](validation/p6-child-synchronization.json)。
