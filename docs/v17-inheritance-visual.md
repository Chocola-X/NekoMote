# V17：继承视觉抽核（静态范围，已复核）

Antigravity / Gemini 3.8 Flash，base `d297827`，提交 `b7fd79a`；Codex于2026-10-09复核。raw：`build/evidence/v17-inheritance-visual/manifest.json`、27裁图、3条带。

## 可保留结论

27原图及裁图hash真实，裁图可逐像素还原到声明的G25 22图/Codex5图。实际三代表静态条件的手/腕/衣袖及遮罩边缘观察可保留：27组alpha差0，零像素超过容差2；25组黑白合成RGB差0，两组Coconut极值差1。所抽静态姿态未见明显挂接断层。范围是default、body_LR±5/15/30、body_UD30及实际body_slant30。

## 必须纠正的边界

三条带均由独立静态PNG拼成，不是同实例时间序列。逐面板像素核查发现实际顺序为 `default,-30,-15,-5,+5,+15,+30`，与旧manifest/报告声称的 `-30,-15,-5,default,+5,+15,+30` 不一致。原raw保持不改，纠正记录在本页和Codex整合证据。撤回“动态连续性/无帧间跳突/平滑单调过渡”认证；不凭这些条带宣布插值或物理正确。

V17未覆盖其余12旧FAIL的视觉，也未认证45扩组合、真实动作/暂停恢复/locator。Codex后续补验与正式放行范围见[整合记录](review-g25-integration.md)；不能将后续证据倒记为V17已执行。
