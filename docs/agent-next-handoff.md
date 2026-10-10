# 下一批独立任务

基线为本轮Codex G22/G24/G26复核提交，见[复核](review-g22-g26.md)。GLM承担开发与批量运行大头，Gemini实际看图，Codex集中语义/生命周期与最终抽核。先声明base/文件；原素材只读，输入只用应用回调；GPU用 `build/gpu-validation.lock`。每阶段验证后commit，真实Agent/Model/Context；报告≤一页，raw放新build目录，冻结输入/工具/二进制/命令/日志/输出。失败只挂起对应条件，继续独立任务。

## GLM G27：完整、可搬移的Plasma部署（已完成）

报告见 `docs/g27-plasma-deploy-report.md`。CMake install()+相对RPATH+独立动态宿主验证全部通过。下一步交Codex复核部署/服务边界，Gemini V20在真实桌面观察。

## GLM G28 → Gemini V19：重建可消费的触摸证据

范围：`tools/touch_audit`、对应Python测试；固定步/seed及overlay坐标已由Codex修，勿重新放宽晚到固定步请求。参考新 `build/evidence/codex-review-g22-g26/` Fraise包；旧G24 raw不覆盖。

1. 三型号×五条件重拍。显式等待帧完成并校验executed步数/FBO/首delta/seed；freeze脚本拒绝旧步数日志或失败，即使PNG存在。源/session字段与42点请求响应hash冻结；保留clean/overlay。校验下身及相关椭圆都入画，必要时新增明确缩放条件，不能沿用“完整入画”假设。
2. 为新Qt条件建立显式证据manifest和观察输入契约，迁移`generate_v9_ledger.py`调用者，接受新冻结条件但**不删除input/tool/binary/asset/png/hash/点绑定验证**。合成负例覆盖错hash/错条件/缺probe/缺source/步数错/不一致PNG。验证器必须拒绝旧V15未绑定观察；用合成有效观察证明成功路径，不代替真人观察。
3. 实际Qt图至少默认和idle120对同输入player/WebGL；不能用native自身图冒充参照。将证据包和空观察模板交Gemini，重拍/视觉/候选状态分别报告。

Gemini V19：逐图检查五部位边界、镜像/坐标/透明遮罩及真实点击点；发现区域偏差先记录区域与偏差/证据，不直接改session或模型。填写实际验证器可消费观察，运行ledger证明被接受。补V18未绑定的12型号24静态与3idle120观察（已有Codex冻结证据，不需盲重拍），不要把终态当连续性或认定bust差异无公式反例。

## GLM G29：动态、locator、分配与195覆盖补验

范围：`tools/g26_validation`及小型诊断工具/测试；如需改runtime/application接口先写契约交Codex，避免与C8求值文件冲突。

- 旧42静态/9动态终态不无理由重跑。三个bust_LR20标FAIL并保留真实输入/参照default相等、native变化证据给C8，不按字段名豁免。
- 在有效session上真正调用pause/resume，核实暂停不推进、恢复首delta0、250ms预算、与相同步数连续播放的状态；60/120两个新实例前缀只做确定性。用受控宿主时间，不注入系统输入。
- locator与原SDK/作者坐标对照，再核实模型原点/镜像/缩放/输出中心/DPI下的触摸投影，不用“移动了”作PASS。数值缺字段/错拓扑/缺通道/进程失败都必须FAIL，添加负例；动作混合/过渡及中途事件采集多个明确步数的几何/图，冻结所有输入/输出/工具hash。
- 建真实模型预热后allocation计数（runtime、renderer、application、UI和设备线程覆盖分别报告），采样阶段不截图/I/O；时间为帧采样而非进程总时长。单进程RSS及实际硬件/driver，记录配置；ASan无窗口preset执行相关真实模型和核心，保留argv/log/hash，未跑不声称无泄漏。
- 分批生成195结构/初态/动态/交互结果矩阵，失败/未跑/缺素材分别计数。不是每型号只一张图；先冻结代表任务/阈值，再扩展。先短循环，长期8–24h在实际桌面功能稳定后串行执行，不与其他GPU任务抢占。

## GLM G30：正式壁纸应用服务（G27后可开始）

先阅读`docs/abi-contract.md`和application现有声音/背景服务，提出最小契约及改动文件交Codex复核，然后实现。正式Plasma不得复制PCM/线程/反应策略；建立共享服务与宿主资源销毁/暂停/失败契约。按声音/背景→UI/角色与背景渐变/reset分别提交，验证音频设备失败、静音/卸载、暂停250ms共同预算、失败事务与恢复，不为旧ABI留兼容层。视觉交Gemini；实际桌面、长期与195覆盖不是这些单项的自动PASS。

## Codex C8 / Gemini V20

C8：先诊断三bust反例的SDK输入/绑定与native实际语义，未确认原因不改正式公式；剩余真实嵌套/2标量反例保持隔离拒绝并建立最小复现。审G27部署/服务边界、G28证据契约、G29抽核与最终整合。

V20：G27通过后观察搬移prefix动态包的加载/config/错误恢复/resize/DPI/输入隔离，记录实际图和库路径。真实独立用户会话测试由用户安排后执行；不要向终端/任意系统窗口注入输入。长期和多屏单列。

优先顺序：**GLM G27→G28→G29；Gemini随G28做V19、随G27做V20；G30逐项推进**。GLM无需等Codex解决所有求值余项才能开始其他任务。新部署通过后即可试装，不要求先完成整条路线图。
