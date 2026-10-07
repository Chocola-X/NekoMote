# 原生窗口播放器

`nekomote_player` 使用独立runtime与OpenGL后端，提供模型播放、视线/触摸、声音/口型及PNG背景。当前是开发中的普通窗口壁纸应用；代表范围已通过对照，全部型号、桌面插件和长期运行尚未验收。当前结论见 [整合摘要](review-agents-current.md)，限制见 [known-defects](known-defects.md)。

## 构建与运行

需要显示服务器、OpenGL 3.3、GLFW，以及播放器声音服务的libpulse开发包。UI需要可用字体，优先通过Fontconfig寻找。构建入口见 [building](building.md)。

```sh
cmake --preset release
cmake --build --preset release
# 使用已导入、已验证的代表型号；诊断时不改保存配置
build/release/bin/nekomote_player --assets assets --character chocola-date-a --no-save
# 没有音频服务时额外加 --mute
```

单模型导入及显式运行：

```sh
build/release/bin/nekomote_import \
  ../NekoWebShow/model_conversion/chocola/chocola-date-a/chocola-date-a.json \
  ../NekoWebShow/model_conversion/chocola/chocola-date-a/chocola-date-a.resx.json \
  --output assets/characters/chocola-date-a
python3 tools/import_session/import_session.py --reference-root ../NekoWebShow \
  --model chocola-date-a --output assets/characters/chocola-date-a/session.nks.json
build/release/bin/nekomote_player --model assets/characters/chocola-date-a/model.nkm.json \
  --session assets/characters/chocola-date-a/session.nks.json --fit
```

正式播放只依赖当前资产/背景和设备服务，不需要原仓库、Python、Node、浏览器或asm核心。只接受当前模型schema 5和会话version 1，不自动回退。

## 操作与配置

- 窗口内鼠标视线、左键触摸、右键拖角色、滚轮缩放；空格暂停/继续，F适配，R重置，Esc关闭。
- 目录模式有角色/背景/UI选择；Tab/1–9切角色，B/N切背景，C清背景。切换失败保留原会话。
- 默认30FPS，`--fps 60` 可选择60；作者时钟60Hz，显示FPS与作者步分开。
- `--fit`适配模型；`--scale`、`--origin-x/--origin-y`使用中心原点、Y向下坐标；`--width/--height`设置窗口尺寸。
- `--mute`禁用声音及声音口型；没有设备服务时须显式使用该参数。音频加载/设备错误明确失败。
- `--assets`读取 `assets/config.json` 保存角色；`--character`覆盖选择，`--no-save`保留原配置。已有目录/能出图不等于型号已认证。
- 最小化或无有效输出时暂停；背景与模型共享250ms追赶上限。角色渐变/reset等剩余限制见known-defects。

## 启动6/3的含义

`session failure 6/3` 是Playback/Geometry失败，不是段错误。当前保存的 `milk-summer` 首帧 `■胸揺れ` 使用bm19，正式路径尚未实现该混合。上一轮Codex恢复严格门槛后，过去使用近似能显示的部分型号现在明确拒绝；本批Agent未引入这条求值变化。

错误包含角色、几何错误名与节点路径，CPU首帧在声音/GPU模型资源初始化前验证。不会把渲染被拒绝的指定角色替换为别的角色。不支持型号的恢复界面/能力标记尚未完成；当前可显式选择已验证型号。补诊断不等于修复该型号。

## 生命周期与诊断

模型/会话程序拥有CPU资产，实例借用程序；GPU对象在有效context中销毁，音频Device拥有工作线程及有界命令队列。PCM解码/纹理上传发生在加载边界。声音RMS用于口型，未声称与浏览器分析器逐样本一致。正式帧路径不读回GPU像素。

固定步截图必须静音，独立于性能测量：

```sh
build/release/bin/nekomote_player --model assets/characters/chocola-date-a/model.nkm.json \
  --session assets/characters/chocola-date-a/session.nks.json --mute --transparent --seed 1 \
  --reaction chocola/h01 --capture build/frame.png --steps 36
# 应用内回调控制自己的窗口，不注入系统输入
build/dev/bin/nekomote_player --assets assets --character chocola-date-a --mute --no-save \
  --check-window build/evidence/owned-window-check
```

当前独立验证：Chocola固定步WebGL对照在上一轮零超差；本轮首帧预检改动前后RGBA字节相同，窗口/候选失败保留检查通过。115组是代表范围，不是195型号完整认证；单元测试不替代视觉、声音、资源和桌面生命周期。证据索引见整合摘要及development-log，历史性能数据不作为当前收益结论。

`nekomote_abi_host`仅是静音C ABI诊断程序。其确定步与截图契约仍待G8复核，不用于认证播放器或Plasma插件。
