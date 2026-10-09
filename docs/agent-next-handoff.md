# 下一批独立任务

基线：最新正式继承整合提交，见[本轮复核](review-g25-integration.md)。用户要求GLM承担实现、批量运行和证据整理大头，Codex接管求值公式并做生命周期复核；Gemini实际看图。先声明base/文件，保护共享工作；GPU用build/gpu-validation.lock，原素材只读，输入只用自身回调。每阶段验证后commit，真实Agent/Model/Context，报告≤一页，raw放新build目录。

## 已完成的继承阶段

G25静态部分与V17静态观察已复核；Codex补了其余12型号24静态参考、三代表idle120及正式195/核心/GPU/ABI/player回归，单边界0x7fc现已正式整合。见[整合记录](review-g25-integration.md)。不再从d297827建候选或重跑旧27矩阵；嵌套仍拒绝，45组合和全动态未认证。G25报告并不代表G22/G24已执行。

## GLM G22：实际Plasma包剩余功能与部署（已完成）

G19的初态重建/配置页真实创建/首delta0/40轮旧renderer及最终销毁已由Codex修，不重复推倒；见[g19](g19-plasma-report.md)。包probe是预链接native的实际KDE宿主，不能据此宣布干净动态部署已验证。

本轮完成（见[g22报告](g22-plasma-report.md)）：
1. `--recovery`模式：bad→good→bad→good恢复，model/session/originY/scale切换，旧state/GPU销毁，动作继续推进；非local URL/缺配置/非法scale/origin分别到达对应分支；批量更新只重建一次。
2. CTest `plasma_package_probe`：6测试（bad-asset/invalid-config/capture/lifecycle/recovery/dpi1.5），9默认+1可选计数，每轮诊断隔离，首delta0信号，resize containment/FBO断言，DPI 1/1.5，CPU peak RSS与renderer计数分开。
3. CMake target `nekomote_plasma_stage`/`_clean`：自包含stage（native库/plugin/qmldir/qmltypes/KPackage），`tools/plasma_stage_verify.py`验证+manifest。plugin RUNPATH指向build（patchelf不可用），运行时需LD_LIBRARY_PATH覆盖。
4. `tools/plasma_install.sh --install/--uninstall/--status`：用户XDG安装，不装系统/不改桌面/不重启shell/不复制PCM。

修改：wallpaper_item.cpp setter静默更新+configure_diagnostics分离输入隔离与固定步长；package_main.cpp添加recovery+FBO断言+每轮诊断隔离。141单元/37694断言+9配置+4安全+16恢复+20生命周期全通过。长期/多屏/真实桌面安装/正式声音接入未验证。

## GLM G24 → Gemini V15：真正可消费的触摸视觉证据（已完成）

本轮完成（见[g24报告](g24-touch-report.md)）：
1. 来源冻结：generate_source_reference.py 直接从原仓库 pixelMarker/presentation 重新计算 source_regions，逐字段比较 session。三模型全部 MATCH。bind_point_evidence.py 绑定 42 点 request/response/hash，全部验证通过。
2. Qt/FBO 条件图：扩展 Plasma probe 支持 --origin-x/--origin-y/--scale/--steps/--seed 和 Qt::WindowTransparentForInput。三模型×五条件 15 clean PNG + 15 overlay PNG 全部成功。PNG=800×1080，alpha bbox 非空。
3. 综合冻结：freeze_g24_evidence.py 36 项契约检查全通过。新条件 lower_body_in_frame 不假冒 G13。

修改：package_main.cpp 条件参数+WindowTransparentForInput；wallpaper_item.cpp 放宽 configure_diagnostics rendered_frames 检查。候选触摸数据保持 PENDING。

Gemini V15已由Gemini 3.8 Flash执行完毕，报告见[v15](v15-touch-report.md)。消费G24 15 clean/overlay图，下半身进入画幅排除裁切误判；发现overlay生成脚本缺失窗口中心540px偏置导致椭圆整体上移~540px，标记MISMATCH，候选保持PENDING。交付符合标准schema的`build/evidence/v15-touch-audit/version3_observations.json`。

## Gemini V16：Plasma视觉复核

可先看Codex本轮实际包40轮最终图与V13有效裁图，G22后逐条件核对配置/错误恢复/resize/DPI/输入。区分实际包与独立QML三固定图；常态不同时间PNG不用于固定像素判定。实际执行命令、图/hash冻结，不凭同一静态图宣称播放/长期/无泄漏。

## GLM G26：剩余求值验收与实际模型分配（已完成）

本轮完成（见[g26报告](g26-runtime-validation.md)）：

1. 45组合参照比较：从G25 extended-combo恢复15变量组合×3代表，生成WebGL+native几何/图。42 PASS+3 PHYSICS_UNSUPPORTED(bust_LR物理控制变量)+0 FAIL。最大几何0.000061px，图像零超差。body_slant实际影响56.62px核验通过。
2. 动态验证15/15：动作(挨拶/ピョン)×120步、中途事件(idle60→挨拶60)、暂停恢复(60步vs120步前60确定性)、locator投影(6 active,body_LR=30全移动)。几何≤0.000038px,物理标量≤1.19e-7。
3. 分配计数：runtime P50=0.29-0.39ms/renderer 1.1s/application 0.87s,ASan三模型无泄漏。

入口：tools/g26_validation/run_45_combos.py、run_dynamic_validation.py、run_allocation_count.py。证据：build/evidence/g26-validation/。bust_LR物理控制变量未实现非公式反例；组合差分因模型无显式variable_sources未测；暂停恢复仅验证确定性。

## Gemini V18：新增型号与真实动态观察（已完成）

V18已由Gemini 3.8 Flash执行完毕，报告见[v18](v18-dynamic-visual.md)。消费G26动作终态（挨拶/ピョン各120步×3代表）、45组合遮罩（眼/嘴半开）与局部变化、Codex 12新型号（24条件）与idle120终态图，腕部严丝合缝无8.95px断裂，遮罩无溢色，bust_LR20物理差分局限于胸部。证据冻结在`build/evidence/v18-dynamic-visual/`（含crops与manifest）。终态静态观察不外推全动态连续性。

## 执行顺序与阻塞规则

**GLM G22→G24→G26**。G22/G24未做，优先完成；遇到核心求值问题只挂起对应条件，保存反例给Codex，继续独立工作。允许自己组织无共享文件冲突的子任务，但GPU串行；不得以部分采样交付宣称整张清单完成。每个完成/未跑/失败/待Codex项单列。

Codex负责求值公式/反例、架构/线程生命周期及最终整合；Gemini负责实际视觉。嵌套/2标量反例、正式声音背景、UI/渐变/reset、195完整动态、8–24h/多屏/安装仍在路线图；本轮只有明确范围的继承放行。
