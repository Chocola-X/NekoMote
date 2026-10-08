# G12 触摸诊断工具与复核

更新：2026-10-08。原交付 CodeArts / GLM-5.2，提交 `c13b394`；
Codex 在 `7c055dd` 后独立复核并修补。执行状态见 [当前摘要](review-agents-current.md)。

## 已验证与限制

历史 `build/evidence/g12-touch-audit/ledger.json` 保留：195 行，原报 180 OK / 15 CPU_REJECTED。
只读复算其中 720 条件、7,920 个记录点，投影与当前作者区域一致，反应属于正确几何优先区。
但原工具 hash 与提交不符，原始命令/响应/失败来源丢失，不能当作当前工具的独立运行验收。
原 label 推断的 764 个 hit_zone 错误；重复 label 不能定位部位，Fraise 的头中心实际属于 head。

工具现已修补：完整 catalog 分母；失败 probe 不作为 MISS；按实际椭圆优先级验证反应；
永久保存请求、响应、命令、退出码、日志及源/资产/纹理/binary hash，结束复查来源。
旧输出明确拒绝；GPU 内部持共享 flock；覆盖图与原图分别冻结。
待机条件先推进120次更新再触摸（事件121），不把事件120之前的119次更新称为120步。
35项合成分支测试通过，旧的17项以固定账本断言为主的测试已替换。

新证据 `build/evidence/g12-v9-review-2026-10-08/fraise-verified/ledger.json`：
Fraise B 四条件全部通过，40个中心/内点及4个远外点、4组投影、8张原图/覆盖图。
这是单型号先行验收，不是全目录重新运行或视觉认证；pant部分区域在画幅外，CPU命中不证明屏幕可见。

## 运行与交接

```sh
python3 tools/touch_audit/run_touch_audit.py --reference-root ../NekoWebShow \
  --assets assets --probe build/release/bin/nekomote_session_probe \
  --player build/release/bin/nekomote_player --capture \
  --model fraise-france-maid-b --output build/evidence/<新目录>
```

全目录去掉 `--model`。报告必须在新输出内；不复用原证据路径，不再使用旧机器绝对路径。
工具内部持 GPU 锁，调用者不要再次套同一锁。输入为呈现方向中心输出像素、Y向下，不翻转X。
缺资产/错误/CPU拒绝/无区域/未测分别保留；无区域仍核对外点。
下一批 GLM G13 用修补后的冻结工具执行全目录来源及命中/图像覆盖，见 [协作清单](agent-workboard.md)。
不改核心、反应、资产或外部素材来使账本通过。
