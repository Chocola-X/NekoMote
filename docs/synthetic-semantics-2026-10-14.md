# C18：程序生成模型与独立求值验证

2026-10-14，基线 `693fd3f10a53d6383f5d376d78bcdc142880d9c8`。用户授权用自定义几何模型补齐求值语义，并明确本轮边界：不读、不反编译、不移植原核心。执行与独立只读复核均由 Codex 完成；会话已知模型为 GPT-6 家族，精确变体未暴露。本轮未发布或安装桌面组件。

本轮交付 `discretization=1`、嵌套 `0x7fc` 挂接，以及同步/组合曲面的传播修正。最终 51 组合成模型的几何、拓扑和原生 GL/WebGL 画面全部通过；截图 RGBA 逐像素一致。四组真实模型回归通过。机器记录见 [结果](validation/synthetic-semantics-results.json)。

## 输入与观察边界

`tools/reference_capture/synthetic_model.py` 独立生成层级、参数帧、解析曲面、四色棋盘纹理、JSON/resx/PNG 和未加密 PSB v4。测试模型不含人物，不复制真实角色的层级或纹理。PSB 写入器扩展本项目已有的合成格式测试；格式标签校验使用公开 MIT 的 PSB 格式定义，未使用参考求值核心实现。

参考端只通过公开 SDK 设置变量、位置/缩放及物理开关，观察提交到 WebGL 的顶点、UV、索引、绘制状态和最终图像。`capture.py --psb` 加载显式实验文件，`--trace-geometry` 不读取 shader 源码，并拒绝与 Math、heap、random、clock 执行观察器混用。原 `FreeMoteDriver.js` / `emoteplayer.js` 按原字节提供给浏览器并记录 hash；未阅读或修改其核心源码。参考端的输出不作为原生求值输入：原生端独立导入相同生成 JSON，求值后才做比较。

捕获超时保存输入、console 与错误为 `.failure.json`，防止错误的测试层级/终止帧使工具一直挂起。PSB 和图像仅在忽略的 `build/evidence/` 内生成；可提交 fixture 只保留小型数值和来源 hash。外部原仓库保持只读。正式运行时和构建不依赖 `refer/`。

## 已验证公式

### 离散参数

绑定开启且 `discretization=1` 时，先将输入朝零截断，再裁剪到作者范围，最后映射到参数时间：

```text
v = trunc(input)
t = (clamp(v, begin, end) - begin) / (end - begin) * division
```

沿用现有 `end > begin` 范围契约与帧插值。20 个参考条件同时包含负小数、零附近、上下界外输入和小数范围端点。范围 `[-2.5,3.5]`、division=60 时，输入 -2.49/-1.9/0.99/1.01 对应时间 5/15/25/35。这样能区分 floor、四舍五入及“先裁剪再截断”等候选。0 保持连续；其他非零模式明确 `UnsupportedDiscretization`，整数输入也不能绕过未知模式检查。

### 嵌套形状排除

保留自身顶点曲线链 `S`，世界仿射 `W`，当前挂接原点 `q=S(W(0))`。排除的曲线分为由内向外的链段 `P1…Pn`：

```text
q0 = q
qi = Pi(q[i-1])
vertex = S(W(v)) + qn - q
```

所有排除链段在当前子节点的已解析原点上依次求值；不能借用祖先在其自身原点缓存的位移。旧候选在双边界模型产生 3.4991016px 误差。每次 evaluate 在控制点就绪后保存一次 attachment_offset，顶点只读取该位移，参数/输出变化重新求值。相邻排除边界没有新顶点曲线可排除时仍保留之前的挂接链；sync=0/1 的补充反例曾丢失 2.5px 位移，已修复。

### 同步与组合

父层自身曲面在 `meshSyncChildMask=8` 时进入子顶点曲线和细分路径；1 只改变直接子原点；0 不传播该父层曲面。更早已继承的曲线仍按已有链保留。排除顶点形状与参考所需的细分拓扑分别处理，不能因为子形状保持刚性就擅自删除参考网格。

同域 `meshCombine=1` 只在父层实际有曲面时合并；没有曲面的刚性父层保留。实际合并消费直接父层的曲面后，辅助层的继承从 transform_parent 重新解析，不再重复使用已消费父层的排除链。辅助层非零局部位移/旋转等既有不支持范围继续失败。6 个组合条件覆盖全继承、内/外排除、双排除、旋转和位置同步，不能只靠单边界解析单测放行。

## 纹理大小与 PsBuild

几何语义实验的最小棋盘是 32×32 RGBA8，资源只有 4096 字节，加上模型结构后约 8KB。真实角色 PSB 的体积主要受图集和其他资源影响；小模型的体积本身不能说明求值条件缺失。

本轮另设 2048×2048 RGBA8 图集（原始像素 16MiB），将 tile 放在 `(1000,700)`；4096×2048 图集（32MiB）承载 595×921 旋转 tile 和 7×11 网格。这两组几何/图像均通过，覆盖大尺寸、偏移、UV、裁切及拓扑。图集之外的透明区域仍保存原始 RGBA 字节。PNG 压缩文件小不代表 PSB 内原始资源小。生成器明确拒绝越界、负数及非整数矩形。

基线和 2048 图集另外调用用户提供的 `PsBuild.exe` 生成真正的 `model.pure.psb`，再让原 WebGL 播放器加载。独立编码器和 PsBuild 的 PSB **不逐字节相同**，因此撤回早期误读输出路径所得的“逐字节一致”结论；实际核对的是两种产物最终几何和图像的一致性，两组均通过。PsBuild 的输出落在进程工作目录，命令必须在独立实验目录运行。使用本轮隔离 Wine prefix，未修改用户原 prefix。

## 冻结结果与失败记录

最终证据目录 `build/evidence/semantics-20261014/accepted-matrix/` 保存三份原生二进制副本、全部输入、来源/tool/source hash、argv、日志、参考/原生截图、submitted trace 及逐组结果。GPU 对照使用 `build/gpu-validation.lock` 串行执行。原生截图为真实 preview GL 后端输出，没有用 CPU 自绘代替。

| 验证 | 结果 |
| --- | --- |
| 合成矩阵 | 51/51；最大几何误差 0.000122606px，门槛 0.001px；无缺失/多余顶点、图元或三角形 |
| 合成截图 | 51/51；所有 RGBA 差值为 0；冻结容差仍为 2 |
| 真实模型 | Chocola date-a default/body_LR=30、Fraise france-maid-b default/head_UD=10，4/4；最大几何误差 0.000218473px，RGBA 最大差 1 |
| PsBuild 交叉验证 | 小图集/2048 图集，2/2；编码器与工具产物参考截图一致 |
| Release CPU | 197 cases / 43640 assertions；实际 Chocola 导入已执行 |
| ASan/UBSan | runtime 76 cases / 14498 assertions；无窗口、leak 检查开启 |
| ABI | 3/3 CPU 契约 runner |
| Python 工具 | 110 项：98 PASS，12 Qt opt-in Skipped；新增生成器 3 项 |
| 稳态分配 | 新的嵌套挂接/离散参数更新单测零普通 C++ 堆分配；不推定整应用/设备线程零分配 |

`matrix`、`frozen-matrix`、`combined-matrix`、`adjacent-matrix`、`review-fixed-matrix` 保留失败候选，不能计为最终通过。Fraise 的一次错误输入 `face_UD=10` 在原生端明确失败；该变量不存在于此型号，改用已列出的 `head_UD=10` 后另开证据目录，不覆盖失败。早期缺少 root priority 终止帧等输入导致的超时/abort 也保留为工具诊断，不推导为引擎公式。

独立只读执行者复核了最终源码/生成器 hash、51 组记录与组合 fixture 8 个角点；复核提出的相邻边界、组合覆盖和矩形校验问题已补实验/修复。未读取参考核心。参考复用校验 PSB、输入、驱动/浏览器及四份捕获文件 hash；合法基线复用通过，仅追加换行改变 trace 文件即明确拒绝（预期退出1），两份记录另存 `reuse-check` / `reuse-rejected`；浏览器路径表示变更会明确拒绝复用，应重新捕获。

## 性能范围

AMD Ryzen 7 5800H、GCC 15.1.1、Release O3/LTO；当前 KDE Wayland，参考 GPU 为 RTX 3060 Laptop、NVIDIA 575.64.03。本项只测 CPU `evaluate+build_frame`，单 CPU affinity=0，预热 120 次，每轮 5000 次，基线/候选交替三轮。没有呈现分辨率/FPS、GPU 读回或 profiler；测量不是整个壁纸功耗/显存基准。两版都用同一真实资产与初态变量，基线在隔离 worktree 构建。

| 模型 | 基线 P50/P95/P99 ms | 本轮 P50/P95/P99 ms |
| --- | --- | --- |
| Chocola date-a | 0.175512 / 0.200864 / 0.223982 | 0.160566 / 0.179912 / 0.198699 |
| Fraise france-maid-b | 0.221468 / 0.262814 / 0.308001 | 0.166363 / 0.205753 / 0.218535 |

表为三轮分位数的中位数；单轮尾部有波动，原记录完整保留。移除不传播父曲面造成的多余细分后，代表模型 CPU probe P50 降低约 8.5%/24.9%，这是局部求值路径收益。没有声称 GPU、整应用、显存或功耗改善。新的挂接遍历只发生在 evaluate；正式绘制没有新增 CPU 像素读回、资源上传或分配。

## 复现与后续

```bash
cmake --preset release
cmake --build --preset release --target nekomote_import nekomote_pose nekomote_preview nekomote_unit_tests
flock build/gpu-validation.lock python3 tools/reference_capture/validate_synthetic.py \
  --reference-root ../NekoWebShow \
  --browser /path/to/chromium \
  --importer build/release/bin/nekomote_import \
  --evaluator build/release/bin/nekomote_pose \
  --preview build/release/bin/nekomote_preview \
  --output build/evidence/c18-new-run
```

输出目录必须不存在。`--case NAME` 可重复，只选定需要的补充条件；`--reuse-reference DIR` 验证来源/文件完整性后复用捕获。无模型素材/浏览器/GPU 时此集成矩阵不能计为 PASS，CPU 单测和生成器测试可以独立执行。

生成最小模型：`python3 tools/reference_capture/synthetic_model.py --output build/evidence/my-rig`。在该目录执行 `wine /absolute/path/to/PsBuild.exe model.json` 可得到 `model.pure.psb`，再用 `capture.py --psb ... --trace-geometry`，明确指定参考根目录、浏览器和输出。正式支持语义无需 schema 更新或资产重建。

下一步继续用程序生成的单变量模型研究 `mesh.cc`、其他离散模式、frame.type1、其他继承位和变形组合，不凭字段名放行。frame.type1 和 mode2 的少量位置探针只构成线索，未纳入正式接受范围。本轮不关闭既有 3 个 bust_LR20、两镜像/非零 X 标量反例、195 全动态、物理/遮罩全组合或长期桌面验收。
