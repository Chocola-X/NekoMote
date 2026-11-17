# 壁纸可靠性改进与验证（2026-11-17）

本轮范围按用户指定顺序：音频故障隔离与恢复、锁屏/关屏节能、独立壳热插拔恢复。
各阶段分别验证、提交；资产目录重新扫描和额外控制接口不在本轮范围。

## 1. 音频故障隔离与恢复

基线 `7430f20`。原实现创建音频设备时在图形线程等待最多6秒；独立 player
会因连接失败退出，共享壁纸实例遇到运行中音频故障也会返回播放错误。
现在设备在音频工作线程按1、2、4、8、16、30秒退避重连，不等待图形线程，
失联时RMS为0。共享壁纸和player继续推进模型。队列满或失联时消费并丢弃
该次语音，不在恢复后补播；连接代次隔离失效前的命令，暂停状态独立保存。
静音仍由用户控制，不把服务失联写成持久静音配置。输入/PCM结构错误仍正常报错。

验证环境：本机 Linux/X11、E5-1680 v4、RTX3080Ti/NVIDIA580.82.09，
GCC Release `-O3 -DNDEBUG -flto`，PulseAudio16.1私有Unix socket + 8000Hz单声道
null sink。测试服务及其运行目录由工具独立拥有，不修改用户服务、输出设备或桌面配置。

- Release/LTO/Werror构建 player、音频测试、ABI边界测试通过。
- `audio_device_contract`、`abi_negative_test`、`abi_test`、`abi_synthetic_boundary`通过。
- `tools/test_audio_recovery.py`：已有播放、暂停时钟、自然结束、反复播放与销毁
  199项断言通过；新增首次无服务、运行中断线/重连、失联暂停、过期语音不补播、
  重试期间销毁不超过250ms，共127项断言通过。
- 缺失音频服务运行真实 Chocola `casual-b-with-bell`，OpenGL、800×600，预热120帧，
  采样180帧正常退出；CPU推进/准备/提交P50/P95/P99为1.254/1.537/1.665ms，
  实际59.668FPS，RMS0。此项证明动画继续播放，不作为前后优化收益比较。
- 独立设备10秒无服务探针（同编译选项）：旧版CPU0.762ms，新版1.815ms；
  新版创建0.093ms、销毁0.126ms。旧版不恢复，新版完成4次连接尝试，新增约1ms CPU/10秒。
  这只是设备故障路径测量，不包含加载、渲染、桌面合成及功耗。

复现私有服务测试（需PulseAudio程序和模块；可以使用隔离解包路径）：

```sh
python3 tools/test_audio_recovery.py --pulseaudio /usr/bin/pulseaudio \
  --modules /usr/lib/pulse-17.0/modules \
  --tests build/release/bin/nekomote_audio_device_tests
```

未覆盖：真实用户音频服务重启、蓝牙/USB音频设备切换、跨发行版/ARM64、长期与功耗。
