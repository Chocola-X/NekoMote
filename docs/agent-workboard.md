# Agent 协作与下一批任务

更新基线：`cb7e677`（2026-10-07）；复核结论见 [本轮接续报告](review-followup-2026-10-07.md)。用户会在本轮之后分别启动 CodeArts 和 Antigravity；本清单不表示已通过工具启动他们。接手时先看实际 HEAD/Git 状态和本文件，不能继续执行已替代的旧任务。

## 当前验收状态

| 原任务 | 实际结果 | 后续所有者 |
| --- | --- | --- |
| V1 / R1，真实 add_mask | Gemini修复已由Codex独立验收；115/115冻结矩阵通过，Fraise400零超差像素 | 已闭合；嵌套遮罩另列待办 |
| V2 / R2，手部0x7fc | Gemini视觉报告完成；数学公式未闭合，Codex撤下近似，正式路径明确拒绝 | Codex继续公式与数值验证 |
| G1 / R3–R4，ABI | GLM建立RAII/错误返回/字段复制，Codex补异常边界、长度、失效、预分配与真实负例；当前ABI0.3.0 | 主边界已验证；源路径诊断/真实宿主生命周期仍待Codex复核 |
| G3 / R6，会话和切换 | GLM修复触摸能力/背景暂停；Codex统一250ms时钟并修复C++播放器候选事务 | 背景reset、渐变重入和角色过渡时钟/合成待V4/Codex；C宿主由G2替换 |
| G5，性能 | 直接改写外部index.php已移除；采样方法仍未通过验收 | GLM后续G5 |
| G6，构建 | GLM修复C++专属警告和Fontconfig条件；Codex完成headless ABI/ASan与无素材副本验证 | G2完成后GLM清理剩余warning/依赖/格式 |
| G4、G8，目录/打包/UI | 未验收；195产品目录、跟踪素材、便携工具和UI稳态仍有缺口 | 本批先做G4、V5；G8随后依据视觉报告开展 |

当前证据：dev136/136、headless ASan135/135、无assets/refer独立副本134 PASS + 1真实素材项Skipped。详见 [runtime矩阵](validation/integration-2026-10-07.json)、[ABI契约](abi-contract.md)、[播放器事务](validation/integration-player-2026-10-07.json)。CPU单次新基线不代表WebGL收益或壁纸性能。

## 分工与文件所有权

这是一项项目调度策略，没有统一的模型能力评测；不把模型名字当作正确性证据。用户有无限GLM额度，优先安排契约明确、可自动验证的工作；具备读图工具的Gemini集中做视觉和源GPU观察；Codex处理未知求值、生命周期与整合。若任务重复失败或指导成本超过直接修复成本，由Codex接管，不增加审批流程。

| 执行者 | 本批顺序与主要文件 | 避免交叉修改 |
| --- | --- | --- |
| CodeArts / GLM-5.2 | **G4 → G2 → G6**；有余力继续G5。`tools/import_all_assets.py`、`import_all_test.py`、`import_emote/audit_catalog.py`、目录账本、`.gitignore`及素材索引；`apps/abi_host/*`；随后构建/依赖文件 | 不改runtime公式、render_gl、C++播放器main、Gemini的视觉工具/UI；G2的只读容量getter是下方限定例外 |
| Antigravity / Gemini 3.8 Flash | **V3 → V5**，可以先做V5；源GPU观察/受控截图、`tools/reference_capture/trace.js`及新的视觉诊断工具、视觉报告；有证据的小型UI修复限`apps/player/ui*` | 不放行bm/attr/0x7fc，不改runtime/render_gl/ABI、播放器main或GLM导入工具；涉及新构建目标先记录其修改范围 |
| Codex / 会话提供的实际模型 | 0x7fc精确挂接、嵌套遮罩、源混合映射；ABI/音频/宿主生命周期复核；V4共同渐变契约、实际Qt Quick/Plasma图形接入 | 后续读取其他Agent的新提交再整合，不重写历史或丢弃其工作 |

主任务相互独立。长采样先复制冻结可执行文件，记录hash。GPU任务串行：本机有`flock`，可用`flock -n build/gpu-validation.lock <命令>`；拿不到锁时继续CPU工作，不能同时跑截图/性能。编译与长采样不能竞态替换采样中的二进制。共享高风险文件先在development-log声明领用；用户可能另有新提交，不能盲目reset。

## 给 GLM-5.2：G4 可移植导入与195目录账本（第一优先）

问题是工具仍硬编码本机路径、按目录第一个文件猜素材，而且已被git跟踪的素材不因.gitignore而自动退出索引。

1. 给现有批量导入/测试入口加入显式`--reference-root`、`--importer`、`--output`及需要的session工具参数；模块导入时不读取素材、创建临时目录或启动任务。不要添加旧路径fallback。
2. 从`config/model-catalog.json`逐项驱动。复用/提取`audit_catalog.py`的目录及alias选择逻辑，不能另造os.listdir首文件猜选。每项记录catalog ID、角色/型号、实际source/resx/纹理来源及hash；歧义明确失败。
3. 新bundle先写到自己的临时目录，模型/session/meta全部验证后一次发布到新目标。失败不得留下可被播放器扫描成成功的partial目录；失败进程退出非零。现有有效输出不覆盖；旧schema或partial明确提示重建到新目标。保护用户已有生成目录和其中额外文件。
4. 为195条产品目录保留独立`structure/cpu/visual/interaction/soak`状态。缺素材也占一行，`milk-winter`不能从分母消失。源目录可能超过195，不能当作产品分母；CPU成功不能填视觉PASS。当前0x7fc/bm/attr拒绝是已有语义限制，应记录字段/节点，不能改runtime使账本更好看。
5. 用`git ls-files assets`盘点，把已跟踪的生成模型JSON、纹理、背景、音频和图标移出索引，**保留磁盘原文件**和必要`.gitkeep`；不要改历史。小型合成测试fixture不属于这一素材清理。
6. Coconut-dress的纹理修复要记录原PSB、解包工具/输出/hash和导入过程；不得修改原仓库或复制全素材进受跟踪源码。资料不足则留未认证状态。

验收：从第二个不同路径的临时源码目录，用显式同级reference-root运行小型成功/歧义/缺失/partial/旧版本/失败退出测试；真实catalog输出195行且每项可追溯。先小范围验证工具，再完整审计，逐项检查点；长审计冻结importer/auditor。不要求本批认证195个模型的画面。开发生成真实bundle到忽略的`assets/characters/`，报告到`build/evidence/`。

## 给 GLM-5.2：G2 精简C验证宿主（第二优先）

当前`apps/abi_host/main.c`复制WAVE/Pulse/RMS和catalog/壁纸策略，活pthread/设备对象被浅拷贝。上次失败回退仍先销毁旧音频，且错误只打印后继续；不适合继续补丁修补。

**本批将它收敛为显式静音的C ABI诊断宿主**，正式声音和壁纸会话仍由现有C++播放器/`audio::Device`提供；不是削减产品声音目标。真实C设备服务等到首个有需求的外部宿主再定义，不建立第二套实现或隐藏静音fallback。

- 删除该C程序中的PCM/WAVE/Pulse/thread/RMS实现和角色目录/壁纸应用副本。使用`--model`、`--session`、`--mute`、seed、尺寸、固定steps/指定reaction等明确输入；静音模式设`NEKOMOTE_AUDIO_DISABLED`。未请求静音则明确说明这是诊断工具，使用正式player；设备失败不能被当作静音成功。
- 保留纯C的CPU会话、GL加载/纹理、固定作者步与预览用途；所有renderer/session返回值检查，缺纹理/不支持模型/prepare失败即非零退出，不打印后继续画旧帧。路径有长度和截断检查；不写系统设置或自动保存用户壁纸配置。
- 生命周期为program先于session创建、后于session销毁；GPU资源先于其拥有上下文销毁。部分加载失败统一收尾，不能浅拷贝拥有句柄。单模型验证不需要角色切换或另一个声音worker。
- 预分配不能硬编码65536等容量。如果需要C端容量查询，允许只在`abi.h/abi.cpp`加入一个**只读**`nekomote_program_renderer_capacity`，在现有`nekomote_renderer_capacity`声明后定义；按`scene.frame_capacity()`及textures数量逐字段返回，NULL返回全零并设InvalidInput。补合成CPU容量负例，不能改其余ABI、错误/帧契约或添加兼容签名。这是此批共享文件的限定领用范围。
- 截图/GL读回仅用于显式诊断，不进正常帧路径。用应用内诊断或确定步数自动退出，不使用xdotool。验证最终帧与同输入的C++player/WebGL已冻结图像，不能只检验窗口创建成功。

验收：无需Pulse/pthread依赖的C宿主；合法合成/真实模型、缺模型/session/纹理、非法尺寸/模式、失败退出和关闭清理；`--mute --no-save`等迁移以新的明确CLI为准，不保留旧参数别名。需要GPU时拿共同锁。正文写清它是ABI验证工具，不是Plasma插件。以后确需C音频时必须复用现有服务，另交Codex评审契约。

## 给 GLM-5.2：G6 构建与格式，随后G5测量

G6在G2简化后移除其`-Wno-format-truncation`/项目warning掩盖，按`.clang-format`整理余下代码。核查Fontconfig/FreeType/Pulse等实际目标依赖、版本/许可与安装说明；公开头不泄漏依赖。验证无assets/refer副本、ABI关闭的纯CPU、headless ABI、preview开/player关、完整player及ASan CPU ABI。缺真实资产只有集成项可Skipped，合成负例必须执行。

G5可独立开始修改`tools/perf_compare/*`：外部原仓库保持只读，显式临时服务目录；两端对齐ready、预热完成、测量起止握手。固定型号、动作、seed、背景、声音/UI、分辨率和实际FPS。浏览器rAF不等于模型更新/实际呈现；RSS共享重复、整卡GPU/显存与进程指标分别标记，缺数据不能填0或生成成功结论。timeout/加载/设备失败必须非零并保存失败。先用stub验证握手/退出/清理，再串行真实采样，记录原始数据、工具/二进制hash、驱动和采样边界。旧性能收益仍未认证，不能宣称此批优化收益。

## 给 Gemini 3.8 Flash：V3 源混合与颜色保护证据

正式路径现在明确拒绝非零bm、attr非0/1；不要删除拒绝来获得图片。GLM的目录任务不是前置依赖，可自己读取现有JSON筛选最小样例。

- 首批选择实际出现`bm=16/19`和`attr=2`的最小型号、层级、资源和触发动作；输出源字段路径/帧/参数范围和hash。若同一型号也有0x7fc，分开记限制，不能绕开门槛。
- 在未修改原PSB的WebGL端冻结输入、初始化时钟/seed、步数和物理；观察真实draw的blendFuncSeparate、blendEquation、颜色mask、目标FBO/附件、纹理/UV、顶点颜色及相关uniform。可扩展自己的`trace.js`，不要读取私有驱动函数体。全局最终状态不能替代每次draw的状态。
- 以父子层/UV/纹理及受控变量定位目标draw，保存透明PNG和黑/白背景合成、局部裁图/差值。推测blend含义与实际观察分开，不按数字映射Add/Multiply等枚举。
- 已支持模型另外验证mirror/global-alpha端点和中间值、眼缘遮罩与视线/触摸坐标。若当前诊断入口没有所需控制，交付明确的缺口和输入，不把OS键鼠注入或参考结果灌入原生帧作为解决方案。

交付：可独立复跑的capture/trace调用、source/input/tool/binary hash、图像与几何报告；源观测和原生Unsupported分别记录，不写“引擎已支持”。Codex据此实现原生映射并通过冻结阈值后才能放行。

## 给 Gemini 3.8 Flash：V5 UI视觉、输入与小型修复

先用已通过的`chocola-date-a`，避免catalog自动选到尚不支持的型号。原生入口示例：

```sh
build/dev/bin/nekomote_player --assets assets --character chocola-date-a \
  --mute --no-save --width 800 --height 1080 --ui-open --ui-drawer 0 \
  --steps 0 --capture-ui build/evidence/v5-next/open-character.png
```

记录字体、device scale与尺寸，检查收起/展开、角色/设置抽屉、反向开关、图标/面板拖动和边界、HiDPI、声音/锁定/FPS状态；UI捕获输入时不能触发模型，外部视线/触摸应正常。网页/原生UI不同排版与字体不能声称逐像素一致；模型本体的2/255及.001px门槛保持。

静态capture已有；实际拖动/中途反向要从应用内状态/回调诊断进入。可以在`apps/player/ui*`或独立诊断工具建立可重复输入和状态测试，禁止系统键鼠注入，不改Codex拥有的main/runtime/ABI。缺接口则报告所需最小入口。已定位且边界清楚的小型UI修复可以自主完成，必须附before/after和实际状态测试；跨模块/GL生命周期改动交Codex。G8根据此报告再做绘制分配/字形预热，不能引用核心136项当作UI测试。

## Codex后续与其余待办

1. 精确闭合Coconut/Azuki的0x7fc挂接及旋转/子节点公式；此前Azuki候选虽图像零超差仍有.0064996px误差，不得作为通过。现有视觉V2不再要求Gemini重复，必要数值采样由Codex组织。
2. 依据V3实现源混合/保护映射；调查Fraise-b缺失/嵌套遮罩。已通过115组不认证这些新范围。
3. V4共同契约：角色过渡0/.1/.2/.4s与背景0/.4/.8s，重入、暂停/恢复、大delta、reset和加载失败；明确线性混合与当前两遍over的区别。先冻结行为再请视觉Agent验收；本批C3只认证候选事务，未认证完整cross-fade。
4. 复核G2设备/ABI边界和G5测量方法；补更精确源错误诊断。核心/工具通过后，读取实际Qt Quick图形后端并做小型宿主验证，再推进WallpaperItem/QML；不改变全局桌面设置或走CPU像素桥接。
5. 完整195视觉/反应、实际桌面、多屏卸载/重建、8–24小时、安装/来源许可待后续阶段；没有实测就不标P9/P10完成。

每个Agent按小阶段验证后commit，正文含`Agent:`、`Model:`、`Context:`；保存FAIL与剩余未知，更新development-log，本批完成后由Codex复核。不要覆盖本轮证据或把旧任务反复实施。
