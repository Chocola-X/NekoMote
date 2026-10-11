# 原生窗口播放器

`nekomote_player` 使用独立 runtime 与 OpenGL / Vulkan 1.0 后端，提供模型播放、视线/触摸、声音/口型及PNG背景。当前是开发中的普通窗口壁纸应用；代表范围已通过对照，全部型号、桌面插件和长期运行尚未验收。当前结论见 [整合摘要](review-agents-current.md)，限制见 [known-defects](known-defects.md)。

Controller、CPU UI 和 GL 执行已拆分，player 已使用这些模块；面板业务仍与 ABI 组合层重复，完整边界验收尚未闭合。设计要求见 [专项交接](wallpaper-application-handoff.md)，当前缺口见 [known-defects](known-defects.md)。

## 构建与运行

需要显示服务器、OpenGL 3.3、GLFW，以及播放器声音服务的libpulse开发包。Vulkan 为可选双栈构建，使用 `--backend opengl|vulkan` 选择，见 [构建、同步和验收说明](vulkan-backend.md)。UI需要可用字体，优先通过Fontconfig寻找。构建入口见 [building](building.md)。

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

正式播放只依赖当前资产/背景和设备服务，不需要原仓库、Python、Node、浏览器或asm核心。只接受当前模型schema 6和会话version 1，不自动回退；旧资产重建入口见 [C5交接](c5-mirror-mask-handoff.md)。

## 操作与配置

- 窗口内鼠标视线、左键短按释放触摸、左键长按 300ms 后拖角色、滚轮围绕指针缩放；空格暂停/继续，F适配，R重置角色（保留背景及渐变），Esc关闭。
- 目录模式有角色/背景/UI选择；Tab/1–9切角色，B/N切背景，C清背景。切换失败保留原会话。
- 默认30FPS，`--fps 60` 可选择60；作者时钟60Hz，显示FPS与作者步分开。
- `--fit`适配模型；`--scale`、`--origin-x/--origin-y`使用中心原点、Y向下坐标；`--width/--height`设置窗口尺寸。
- `--mute`禁用声音及声音口型；没有设备服务时须显式使用该参数。音频加载/设备错误明确失败。
- `--assets`读取 `assets/config.json` 保存角色；`--character`覆盖选择，`--no-save`保留原配置。已有目录/能出图不等于型号已认证。
- 最小化或无有效输出时暂停；背景与模型共享250ms追赶上限。角色渐变/reset等剩余限制见known-defects。

## 启动6/3的含义

`session failure 6/3` 是Playback/Geometry失败，不是段错误。先前milk-summer的bm19缺口已正确实现，默认保存角色的60步截图成功；Fraise maid-a和Milk两季新增完整WebGL播放9/9通过。其他未支持继承/遮罩仍明确拒绝，错误不能一概归因为bm19。

错误包含角色、几何错误名与节点路径，CPU首帧在声音/GPU模型资源初始化前验证。不会把渲染被拒绝的指定角色替换为别的角色。不支持型号的恢复界面/能力标记尚未完成；当前可显式选择已验证型号。型号认证以当前几何/画面证据为准。

## 生命周期与诊断

触摸区域目视检查：

```sh
build/release/bin/nekomote_player --assets assets --character fraise-france-maid-b \
  --show-touch-regions --no-save
```

胸部红、眼部黄、脸部绿、头部青、下身紫；半透明填充与边界随位置、缩放和呈现镜像变化。
只显示配置了反应的静态作者椭圆，不随网格变形。区域重叠时优先级为胸/眼/脸/头/下身；
暂停、反应Busy或UI拦截仍可能阻止触发。宿主输入使用呈现方向的中心framebuffer像素，
Y向下，不再自行镜像。该诊断与 `--measure-frames` 互斥。
确定步截图、判定数据与逐模型核查入口见 [C6交接](c6-input-review-handoff.md)。

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

当前独立验证：旧115+9组冻结WebGL代表矩阵及C5镜像范围已通过；本轮V8重审20标准通过、2标量失败，全部22项几何/图像通过。触摸区域诊断已有4种真实布局截图和Fraise B移动缩放后的命中检查。代表范围不是195型号完整认证；单元测试不替代视觉、声音、资源和桌面生命周期。证据索引见整合摘要及development-log，历史性能数据不作为当前收益结论。

`nekomote_abi_host`仅是静音C ABI诊断程序。固定1/60秒、交换前PNG和错误边界已复核，C/C++同初态30步RGBA相同，不用于认证播放器或Plasma插件。
