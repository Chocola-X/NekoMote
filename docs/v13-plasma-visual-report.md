# V13：实际 Plasma 包视觉与配置交互验证报告

基线：`c241af8`（GLM G19）；执行：Antigravity / Gemini 3.8 Flash；证据目录：`build/evidence/v13-plasma-visual/`。依据 [AGENTS.md](../AGENTS.md) 与 [下一批交接](agent-next-handoff.md)，本轮对 G19 交付的真实 Plasma KPackage 壁纸包进行逐项视觉检查、配置交互验证、生命周期稳定性和固定步基准对照。

## 1. 验证结论

- **实际包画面质感**：Chocola (`chocola-date-a`) 与 Fraise (`fraise-france-maid-b`) 在实际 KPackage 宿主（`nekomote_plasma_package_probe`）下渲染完整。五官定位准确、发丝遮罩边缘平滑、双耳绒毛半透明过渡无锯齿；手部轮廓与握带层次正确，无曲面撕裂或层序穿透。
- **配置界面易用性与安全**：`config.qml` 布局规范，提供文件浏览（FileDialog）、本地 URL 格式提示（`⚠ URL 应以 file:/// 开头`）、坐标范围（-8192..8192）和缩放（10%..300%）约束；未向用户暴露任何内部步骤计数或 ABI 字段。
- **错误恢复与 GUI 存活**：坏资产与非法 JSON 均平稳进入 `status=2`，居中渲染浅红错误诊断（`load model: asset load failed`），宿主 GUI 存活且未崩溃退出。
- **生命周期视觉稳定性**：经历 pause/resume（恢复首 delta=0.032s 无追赶）、hide/show、resize (640x480)、20 轮 origin 重建与 20 轮 scale 重建后，最终画面与初态像素级完全一致，无资源泄漏或渲染器退化。
- **QML 与 WebGL 固定比对**：独立 QML 组件在 3 个确定性初态（Chocola 默认、Fraise 默认、Fraise DPI 1.5）下，alpha/黑白背景合成与原 WebGL 参照相比最大像素差均为 **0**（严格通过 ≤2 门槛）。

## 2. 实际 Plasma 包画面目视细节分析

| 检查项 | 观察对象 | 视觉细节特征与判定 |
| --- | --- | --- |
| 头部与五官 | `crops/chocola_face_crop.png`<br>`crops/fraise_face_crop.png` | 瞳孔与高光层次分明，刘海自然下垂遮挡额头，无暗边或杂色 alpha halo；猫耳内外轮廓与绒毛羽化平滑，stencil 遮罩无溢出。**通过** |
| 手部与握带 | `crops/chocola_hands_chest_crop.png` | 双拳紧握项圈缎带，手指关节与粉色缎带、胸前花朵及金属铃铛层次分明；铃铛高光透亮，手腕部未见 0x7fc 类似的曲面二次拉伸变形或穿模。**通过** |
| 服饰与细节 | `crops/fraise_collar_bell_crop.png` | 水手领红边描线与蓝白条纹连身裙线条平滑，金色铃铛与深红蝴蝶结契合自然；无多边形拼缝黑线或图集走样。**通过** |
| 高 DPI 缩放 | `crops/chocola_dpi15_face_crop.png` | 1.5 倍缩放（1200×1620）下，线条密度成比例提升，无非整数采样产生的模糊或视口截断。**通过** |
| 周期重建后 | `crops/lifecycle_final_face_crop.png` | 经历 40 轮 Loader 销毁重建后，画面保持完全一致（SHA-256 与初态完全重合 `fe67c87c...`）。**通过** |

## 3. 配置页与错误呈现验证

| 界面分支 | 捕获图 | 界面表现与状态 |
| --- | --- | --- |
| 标准配置状态 | `config_ui.png` | 标题粗体清晰，模型/会话展示完整本地路径并附带“浏览…”按钮，X/Y/缩放 SpinBox 范围正确，布局紧凑统一。 |
| 非 URL 路径输入 | `config_ui_warning.png` | 输入未带 `file:///` 的裸绝对路径时，输入框下方即时呈现橙褐色警告文案 `⚠ URL 应以 file:/// 开头`。 |
| 缺失文件恢复 | `crops/error_text_bad_asset_crop.png` | 缺失模型路径时，界面中央以 `#ff6666` 文本正确显示 `load model: asset load failed`，底色保持透明，窗口不闪退。 |
| 非法 JSON 恢复 | `crops/error_text_invalid_cfg_crop.png` | 模型内容格式非法时，同样进入 `status=2` 并公开诊断文本，GUI 正常存活并可截取。 |

## 4. QML 与 WebGL 黄金对照（3 固定测试点）

| 测试用例 | 尺寸 | 原 WebGL SHA-256 | QML 产物 SHA-256 | Alpha/黑/白最大差 |
| --- | --- | --- | --- | --- |
| `chocola-default` | 800×1080 | `0f681f4d4f88a5ab...` | `fe67c87c07d420e3...` | **[0, 0, 0]** |
| `fraise-default` | 800×1080 | `94fe9d644b3fd0e4...` | `d88edb2f7a257458...` | **[0, 0, 0]** |
| `fraise-dpi` | 1200×1620 | `003ec8401306354f...` | `42cb48b7acd01132...` | **[0, 0, 0]** |

## 5. 证据产物清单

所有原始证据及日志保存在 `build/evidence/v13-plasma-visual/`：
- `manifest.json`：包含全部 22 张全图及裁图的尺寸、字节数与 SHA-256 签名；
- `pkg-chocola/package.png` (`fe67c87c...`)：实际 KPackage 宿主 Chocola 画面；
- `pkg-fraise/package.png` (`d88edb2f...`)：实际 KPackage 宿主 Fraise 画面；
- `pkg-chocola-dpi15/package.png` (`cbd2a8b1...`)：实际 KPackage 宿主 1.5 DPI 画面；
- `pkg-bad-asset/error_recovery.png` (`6e277896...`)：坏资产错误恢复截图；
- `pkg-invalid-config/error_recovery.png` (`73d1f38e...`)：非法配置错误恢复截图；
- `pkg-lifecycle/lifecycle_final.png` (`fe67c87c...`)：40 轮重建后最终状态截图；
- `config_ui.png` (`7b0bc488...`) / `config_ui_warning.png` (`f2875e8e...`)：配置页渲染及警告显示；
- `qml-fixed/visual_comparison_sheet.png` (`0ac260de...`)：QML 与 WebGL 对照拼图。

## 6. 说明与后续边界

本轮验证基于隔离 KPackage / Containment 宿主及 workspace 临时 XDG 状态，**未**修改系统/用户真实桌面设置，亦**未**重启桌面 shell。正式全功能桌面安装、双屏/多屏壁纸持久运行、正式音频/背景渐变仍属后续阶段目标。
