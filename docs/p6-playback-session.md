# P6 统一播放会话

2026-10-04。共同作者时钟已接通动画、场景锚点、八个物理控制器和最终构帧；完整播放器未认证，P3/P4/P5/P6 继续进行中。正式原生入口直接求值资产和事件，不使用 SDK 参数输出、顶点或截图作为运行输入。

## 运行与所有权

PlaybackProgram 拥有 GeometryScene、AnimationProgram 与 PhysicsProgram；源 ModelAsset 可以卸载。PlaybackInstance 借用 Program，独立持有动画、锚点、物理、参数和帧缓存。全部调用在一个求值线程；GPU 资源与窗口生命周期仍由宿主负责。

作者时钟从 AnimationInstance 移入 PlaybackInstance，删除原动画 advance/pause 接口及重复时钟。每一个 60 Hz 作者步严格按动画 → 绑定动画参数 → 求值锚点 → 物理 → 合成最终参数推进，包含低显示频率下一次 elapsed 中的所有中间步。prepare_frame 再求值最终几何并构帧，重复准备无变化帧直接复用缓存。窗口没有新作者步时不重复上传几何。

单次 elapsed 限 250 ms/15 步；超量丢弃，暂停/恢复清除小数余量，暂停时显式 step 也不推进。非法 elapsed 保持状态。步进或构帧失败使会话进入 Faulted，隐藏旧参数和帧，必须 reset 后才能继续；不能在动画已推进但物理失败时继续错位播放。reset 清空变量、动作、物理和时钟，保留模型放置。非法参数请求不提交；play 的失败可能已经改变多个轨道，因此也要求 reset。

PhysicsMode::DisabledDiagnostic 只用于显式动画隔离诊断；Enabled 遇到未知控制器会拒绝编译，不暗中关闭物理。未知枚举也拒绝。

## 入口

```sh
build/dev/bin/nekomote_preview --playback --asset /path/to/model.nkm.json \
  --origin 400 940 --play 首横振り 1
# 空格暂停/恢复，Esc 关闭；固定步骤用 --steps 24 --hidden --screenshot frame.png
```

--origin 表示初始输出像素放置，转换为模型中心坐标参与 Float32 父矩阵，而最终 view 单独映射画布。当前联合播放只验证 scale=1，其他比例明确拒绝，下一步验证模型缩放与物理坐标；静态/动画隔离诊断原有缩放不变。自动眨眼、selector、部分表情路由及既有几何/遮罩缺口仍未完成，此入口不代表最终壁纸应用。

无显示服务器下可用 nekomote_playback_probe ASSET INPUT_JSON OUTPUT_JSON 逐步采样相同正式会话。输入为 steps、origin、variables、transitions、plays、amplitudes、displacement；dump_frame 显式输出实际帧的几何协议，benchmark 只计 CPU 作者步/准备、不含 JSON 采样。正常热路径不包含 JSON、磁盘、窗口或 GPU。

## 验证结果

- 动画＋Spring＋Chain 的移动锚点，在 30/60/144 Hz 下作者步、全部最终参数及几何完全一致；另核对独立模块逐步调用链。编译数据可卸载源资产，实例独立、重置保留原点、错误隔离和失败恢复通过。
- 120 步预热后，200 次两步追赶＋构帧＋重复准备零普通 C++ new；不宣称覆盖第三方驱动或系统分配。
- Debug/Release 76 项、ASan/UBSan 75 项通过。
- 未修改 Chocola-date-a PSB，全部物理启用。静止 24 步、转头 24 步标量与图片通过；跳跃 24 步标量仍 FAIL（quake_UD_spare，0.000253677368），待机差分 60 步标量仍 FAIL（0.000280708075），差分图片一个 RGB 3/255 像素 FAIL。所有实际会话帧的 UV、顶点匹配和三角形通过，最大 0.000272957517px。转头并列图已查看。阈值仍为 1e−5、0.001px、2/255。
- 原 SDK GetVariable 的主通道缓存不包含差分叠加，不能当作最终动画值。此原始标量比较覆盖 22 个显式观察的物理输出，最终动画混合由实际帧与图片验证；不将缓存差异修饰为正确或作为原生输入。
- Release+LTO、Ryzen 7 5800H/NVIDIA 575.64.03/KDE X11，Chocola-date-a 待机差分、八个控制器、120 步预热、2000 步 CPU 作者动画＋锚点＋物理＋最终几何＋构帧 P50/P95/P99 = 0.287675/0.312329/0.327904ms。采样基于父提交 59aa9d5 的工作树；不含 GPU、实际呈现、截图、JSON 或功耗，不能据此宣布整机性能改善。

冻结输入和源/资产/截图/trace hash：docs/validation/playback-inputs.json；结果：docs/validation/p6-playback-session.json。原参数采样缓存的限制和已有 FAIL 均保留。

```sh
python3 tools/reference_capture/validate_playback.py \
  --reference-root ../NekoWebShow --importer build/dev/bin/nekomote_import \
  --preview build/dev/bin/nekomote_preview --evaluator build/dev/bin/nekomote_playback_probe \
  --reuse-reference build/evidence/p6-chain-binding --output build/evidence/playback-check
```

工具需要 Pillow/numpy；冻结截图位于本地显式 evidence 路径，可按 tools/reference_capture/README.md 重新采样，重采样后的元数据 hash 需审计。已有两组 FAIL 时验证命令退出 1，属于保留的失败证据。几何比较限于该冻结单图集、SDK texture 2 的诊断协议，不能宣称整个型号目录支持。正式构建和测试不依赖这些素材。
