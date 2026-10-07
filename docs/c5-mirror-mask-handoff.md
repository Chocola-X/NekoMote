# C5 镜像、遮罩与物理整合

更新：2026-10-08。基线 `c3a7c33`，复核 G10/V7 并整合正式实现。当前任务见
[协作清单](agent-workboard.md)，机器摘要见
[验收清单](validation/c5-mirror-mask-results.json)。本阶段不是完整引擎或 Plasma 交付。

## 实现与边界

- schema 6 在导入阶段展开 `mirrorControl`；当前实测模式为 `_LR`、`_slant` 子串。
  作者参数、动画及公共观测不取反，几何入口对展开 ID 取反一次，最终呈现镜像保留。
  未知模式、缺少必需字段及旧 schema 明确拒绝，不增加兼容播放路径。
- 遮罩引用在整个模型均无定义时作为可选项跳过；已有定义但不属于当前展开作用域、
  全空 union 和嵌套仍拒绝。Fraise maid-a 的真实 `add_mask` 未被按名称删除。
- 裁剪组按原遮罩与展开 motion 实例建立，范围取该组内容与来源的交集。
  避免 French maid-b 小高光被截断，也保留 Chocola 独立高光的裁剪边界。
- 镜像物理锚点经 Float32 归一化后反转 X，再参与积分；输出变量与外力保持作者方向。
  几何放置预反转镜像模型 origin X，使最终呈现遵循请求的水平放置方向。
  未引入帧内解析、分配或 GPU 读回。
- 删除 V7 硬编码结论后处理器；通用 `build_visual_evidence.py` 只整理已有实际产物，
  不捕获、不推导语义，空帧不得 PASS。图集 hash 捕获为显式诊断开关。

另一执行者只读复核上述坐标与生命周期边界，未发现新增严重架构或热路径问题；
其结论不替代下面保留的精度失败及未验证范围。

## 验证范围

证据根目录为 `build/evidence/c5-mirror-mask-2026-10-07/`，真实素材及图像不纳入 Git。
具体 SHA256、日志和结果位置见机器摘要。

| 检查 | 结果与实际覆盖 |
| --- | --- |
| 发布态旧代表矩阵 | 旧 115 项与上一批 9 项完整播放重跑，124/124 PASS |
| V6 整模型对照 | 19/19 PASS；该批先于最后物理/origin 修正，输入物理幅度与 origin X 为零 |
| 新受控源对照 | 19 项：17 完整 PASS，2 标量 FAIL；19 项几何、拓扑及像素均 PASS |
| 新对照最大几何误差 | 0.0000482525277736px；alpha/黑白合成均无超差像素 |
| Debug / ASan+UBSan | 143/143（含 GPU）；141/141（无显示服务器） |
| Python 工具 | 48/48；含来源定位、迁移拒绝与部分发布回滚用例 |
| 本地应用 | 用户当前默认 azuki-santa、显式 French maid-b、milk-summer：60 步、静音、不保存、退出 0，并截图 |
| Vanilla santa 图集修正 | 静态几何/拓扑及整图像素通过；不是全动画认证 |
| 195 项目录 | 最终二进制重审：结构 195 OK，CPU 180 OK / 15 FAIL；视觉/交互/soak 全部 PENDING |

原生与 WebGL 使用相同作者输入、确定时钟和初态；不再手工取反原生输入。
图像已读取核对默认眼态、动作与实际应用的角色轮廓、眼部和高光。
公共 SDK 的外部差分缓存不包含 `head_LR` 合成值，仅在两个明确差分用例的标量观察中
排除该缓存值，几何及像素仍完整比较；详情在 `channel-observations.json`。

门槛不变：几何 ≤ .001px、拓扑全匹配、标量 ≤ 1e-5、alpha 与黑白底合成 RGB ≤ 2 字节，
无超差像素。原始未关联 RGB 仅作诊断。

发布态局部采样：Ryzen 7 5800H、RTX 3060 Laptop / NVIDIA 575.64.03、KDE/X11，
French maid-b、800×1080，预热 120、采样 2000。动画/锚点/物理/几何/帧准备的 CPU
P50/P95/P99 为 0.940/1.045/1.209ms；固定已准备帧 render+swap 为
0.197/0.265/0.280ms（vsync=0）。二者独立采样，均无截图读回；不代表整个应用、
GPU 实际执行时间、呈现 FPS 或功耗，也不据此宣称相对旧实现提速。
既有预热零分配检查通过；UI 与设备线程覆盖仍待补。

## 保留的失败与未知

`accepted-visual/report.json` 的整体 `passed=false`，不能描述为 19/19 完整通过。

| 反例 | 相同输入 | 最大标量误差 |
| --- | --- | --- |
| force-negative30 | origin [50,350]，scale .8，外力 [-3,+1]，30 步 | 1.6689300537109375e-5，hair_UD_front，第 3 步 |
| force-half-positive30 | origin [50,350]，scale .5，外力 [+3,-1]，30 步 | 1.1444091796875e-5，hair_UD_front，第 26 步 |

两例几何/像素通过，但精度未达标，后续由 Codex 推导，不能放宽阈值。
非零 X 物理快照的源对照尚未完成（实测镜像素材的快照 X 为零）；合成测试覆盖
非单位缩放、origin、移动锚点、正负外力及 reset，不代替该源对照。
余下 CPU 失败、0x7fc、嵌套遮罩、UI/渐变/reset 和实际 Qt Quick/Plasma 验收继续执行。

本轮早期临时脚本误用相同 PNG 输出路径，使 `source-variable.png`、`source-substring.png`
及其旧像素报告无效；随后另路径重新捕获 `*-official.*`，最终验收只引用新文件。
旧 V7 和冻结矩阵未覆盖。其他拒绝候选留在证据目录，仅供溯源，不作为通过证明。

## 素材、迁移与复现

GLM 对外部 Fraise 重解包由用户明确授权，属于授权素材修复。原 `/tmp/g10-v2-*`
账本当前不可用，历史统计不冒充独立重读；本轮建立新账本。

公共 WebGL 的实际 RGBA8 上传与 PNG 解码字节核对发现两处差异：
Coconut dress 的外部 PNG 不符，旧项目 PNG 与官方上传一致；Vanilla santa 则是旧项目
PNG 不符，新解包 PNG 与官方一致。只在项目内修正，保留旧纹理、官方上传 hash 与
隔离导入来源（`texture-repair.json`、`verified-catalog/`），未再次修改外部素材。

本地 195 个模型以 `tools/rebuild_asset_schema.py` 从 schema 5 显式迁移至 6。
发布前验证作者数据及解码 PNG 不变，旧模型与 hash 在 `local-schema5-backup/`；
会话、背景、音频及用户配置保留。13 个冻结输入清单的 143 行仅更新资产 hash，
作者输入与参考 hash 不变；冻结的历史资产未迁移。

```sh
python3 tools/rebuild_asset_schema.py --help
python3 tools/reference_capture/build_visual_evidence.py --help
python3 tools/reference_capture/capture.py --help
cmake --build --preset release
ctest --preset dev --output-on-failure
env -u DISPLAY -u WAYLAND_DISPLAY ctest --preset asan --output-on-failure
./build/release/bin/nekomote_player --assets assets --character fraise-france-maid-b
```

schema 重建输入必须先完成来源核对；不要用当前有问题的 Coconut 外部 PNG 覆盖已验证素材。
批量来源审计交 GLM G11，独立画面复核交 Gemini V8；新任务使用新输出目录并冻结工具。
