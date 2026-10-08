# G14：Qt诊断宿主的自动验收入口

历史契约：入口已实现并复核修补，当前状态见[g14报告](g14-qt-host-report.md)及[协作清单](agent-workboard.md)，不重新领用。
所有者CodeArts / GLM-5.2。G15交付后执行，可与Gemini视觉核对并行。
基线使用本轮C7交付提交，先声明实际HEAD与修改文件。契约见 [Qt宿主](qt-quick-host.md)，
已有命令/像素证据见 `build/evidence/c7-qt-quick-2026-10-08/`。

## 修改范围

新增 `tests/reference_capture/test_qt_quick_host.py`，必要时只修改`CMakeLists.txt`中现有Python测试注册区
注册可选目标测试；报告写`docs/g14-qt-host-report.md`。不改Qt宿主、GPU/ABI、触摸数据、
当前摘要或外部素材。遇到宿主失败保存复现交Codex，不增加兼容模式或放宽门槛。

## 可独立完成的工作

1. 把已冻结CLI错误接入Python unittest：unsupported graphics-api、width=0、missing model、
   已有输出文件拒绝。分别断言实际非零返回码和对应错误，不让缺模型提前遮蔽目标分支。
   已有输出负例用临时文件与完整参数，验证内容保留。新增正常`--help`/缺参数边界。
2. 有Qt二进制和Chocola素材时运行确定30步的`--check-lifecycle`：PNG为预期尺寸，
   初态/重建alpha及黑/白背景合成RGB每通道误差≤2，没有超门槛像素，创建/销毁均为2。
   再以`--capture`验证默认Fraise、移动缩放和1.5倍DPI；与同固定条件GLFW普通尺寸作对照。
   超高GLFW已有裁切缺口，不把它作为DPI金标准；DPI检查实际尺寸及命令响应，视觉以已有
   真实WebGL证据为范围，不在本任务重新创建评级器。图像内容门槛必须实际计算。
3. CTest只在Qt目标开启时注册该GPU集成入口，设置工作目录和gpu/python标签。
   使用CTest显式传入的Qt二进制/启用标记，不自动扫描其他preset现存build；默认Python
   discover也会看到该文件，未显式启用时整组Skipped。二进制/真实素材缺失显式Skipped；
   不把Skipped计PASS，默认/ASan不引入Qt/窗口。
4. 独立目录验证ABI=ON、preview/player/tools/tests=OFF、Qt=ON可配置并构建宿主；
   Qt=ON/ABI=OFF的组合应在CMake明确失败。保留命令、构建日志与配置hash。
   使用显式preset/开关，不改用户已有build缓存或安装系统包。

Python测试使用临时新输出、子进程timeout、原始stdout/stderr/退出码。只调用应用自身诊断接口；
禁止系统输入注入或按窗口标题选择窗口。GPU用共享flock锁；读取图片不并行采样GPU。
不依赖旧截图文件存在作PASS，不把CPU成功认证画面。复用已有PIL依赖，缺依赖明确说明。

## 交付

提交测试/注册与简洁报告，注明真实运行/Skipped/失败数量及来源、构建模式、模型、DPI和二进制SHA。
新增断言须到达实际分支；文档不编造测试数量。小范围工具任务可自行验证提交，正文填写实际
Agent/Model/Context=G14。Codex复核持续检查与Qt边界，Gemini继续V10，不必等待G14。
