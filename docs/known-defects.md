# 当前已知缺陷与验收限制

更新：2026-10-07第二轮整合，源码基线`cb7e677`。旧失败见 [首次审查](review-2026-10-07.md)，当前修复范围见 [接续报告](review-followup-2026-10-07.md)，下一任务见 [协作清单](agent-workboard.md)。未认证项不得放宽阈值或通过改写目标标完成。

| 缺陷/范围 | 当前观察 | 下一任务 |
| --- | --- | --- |
| Fraise眼缘遮罩回归 | **已修复并独立验收**：真实add_mask恢复，Fraise400零超差，115冻结矩阵通过；Fraise-b缺失/嵌套来源仍拒绝 | V1已闭合；嵌套遮罩由Codex |
| Coconut/Azuki局部继承 | 原Coconut8.876191px/13,111像素FAIL保留；近似已移除，正式0x7fc路径UnsupportedTransform。公式未还原 | Codex / R2 |
| bm/非标准attr | 非零bm、attr非0/1已恢复明确拒绝；源GPU混合/颜色保护认证仍缺 | Gemini V3，Codex映射 |
| C ABI边界 | 0.3.0长度/固定宽度/RAII/异常/帧失效/字段复制/预分配与headless已验证；更精确源诊断、真实宿主上下文丢失/卸载未验证 | Codex后续；G2限定只读容量getter |
| C宿主音频/线程与错误流程 | 重复PCM/Pulse、quitting竞争和pthread浅拷贝仍存在，回退先毁旧声音；打印错误后继续不是成功 | GLM G2：收敛显式静音诊断宿主，删除重复服务 |
| 会话/切换/渐变 | 能力查询、背景暂停/共享250ms预算和C++候选事务已修复；角色过渡仍使用墙钟且两遍over，背景reset/重入/声音候选失败尚未验收 | V4 / Codex；Gemini后续视觉 |
| UI状态与稳态 | UI截图/拖动/输入隔离与字形绘制分配尚未完整验证；核心136项不能代替这些测试 | Gemini V5，后续GLM G8 |
| 资产/目录/构建 | G4已完成：catalog驱动事务导入、195账本、显式参数、素材移出索引；C宿主warning/依赖/格式待G2简化后清理 | GLM G6；G2进行中 |
| 性能测量 | 直接改外部index.php已停止；握手/条件/实际呈现/共享RSS/整卡GPU归属等方法仍未闭合 | GLM G5，Codex复核 |


## 局部继承调查的适用范围

早期“coconut-casual 93%差异”来自背景与透明参照混用，不能作为模型误差。本次Coconut maid采用同尺寸/比例/原点、透明输出、相同原PSB零步，排除该条件问题。之前158px畸变下降到8.87px的候选仍然不符合 .001px 门槛。

inheritShape字段含义不自动给出精确原点/旋转/子节点公式。见 [历史分析及新审查标注](coconut-hand-stretch-defect-analysis.md)。实际影响不只五个Coconut型号，应按所有0x7fc结构筛选；其他参数/动作范围需受控复测，不能宣布完整修复。

## 素材来源问题

先前调查称coconut-dress资源映射指向空图，另从原PSB解包取得纹理后重新导入。本次未重做该修复或认证其全部姿态；G4应记录输入PSB、工具版本、解包输出/纹理hash和导入状态，不把本地替换等同于受控来源验收。milk-winter等缺项继续在目录账本占位，不能因为缺文件而从"全目录"分母移除。

### coconut-dress 纹理来源记录（G4）

- 源 PSB：`data/coconut-dress.pure.psb.zip`，sha256 `6dfc2bb95c098267081f6cd3d58d612fecb199ffc4331637d3a38a03ab257d9d`
- 解包模型 JSON：`model_conversion/coconut/coconut-dress/coconut-dress.json`，sha256 `63b02025535aa6d6f301e6e6cba9f9b73f5e2d40ddb1f7007c0f8d518272e19e`
- 解包工具/版本：未知（原仓库外部工具，非本项目工具链）
- 目录内外部纹理：`coconut-dress-a_tex000.png`，sha256 `f6d32dafa122de947640730b40eefa29e20dd2292648f96a9f8a3999c2821e1c`
- resources 子目录：`coconut-dress-resources/tex-texture.png`（32KB）
- 导入状态：结构导入 OK；模型 JSON 中 `tex` 为 inline（4096×2048），external_textures 为空
- 认证状态：未认证。解包工具/版本未知，未重做修复或认证全部姿态；不把本地 inline 纹理等同于受控来源验收

## 尚未开展的产品验收

实际Plasma壁纸插件、宿主后端/多屏/卸载重建、8–24小时运行、完整目录画面/反应、同条件性能收益与安装产物尚未闭合。C/GLFW窗口可用于接口实验，不能认证这些能力。当前115组代表回归已经通过，但不替代新增语义、完整目录、UI、实际桌面或长期验收。
