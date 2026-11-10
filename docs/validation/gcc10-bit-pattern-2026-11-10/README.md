# GCC10 浮点位比较补修验证

基线 `5d75c87`，问题及处置见[CI说明](../../github-actions.md)，经验持续维护在
[汇总](../../optimization-lessons.md)。修复仅涉及测试与审计序列化，不改运行时或渲染器，
没有重测未改动的性能/GPU路径，也没有修改前轮冻结的源代码哈希和性能记录。

用户六份Ubuntu20日志的共同首错是 `test_geometry_scene.cpp` 使用缺失的 `std::bit_cast`。
`verification.json` 保存六份zip哈希和错误摘要、工具链包/修改源码哈希及实际验证范围。

从 `archive.ubuntu.com/ubuntu/pool/{main,universe}/g/gcc-10/` 下载官方
`10.5.0-1ubuntu1~20.04` 的gcc/g++/cpp、libgcc-dev、libstdc++-dev、libstdc++6及libgcc-s1，
另取官方libisl22，解包到 `/tmp/nekomote-gcc10-fix/root/`，不安装系统包。
GCC10/libstdc++10使用其自带头文件，链接后也通过该目录动态加载libstdc++10，见
`gcc10-linked-libraries.txt`；glibc、PNG、字体等库使用主机版本。不是完整focal sysroot，
不据此认证符号上限、发行归档、完整GUI/适配器工作流或aarch64。

先取 `git show 5d75c87:{tests/unit/test_geometry_scene.cpp,tools/anchor_audit/main.cpp}`
作为临时原版，复用Release compile_commands的include/宏/警告参数，将编译器换为官方
GCC10，分别加 `-fsyntax-only`。两文件修复前均退出1、修复后均退出0，无警告。
`*-before.txt` 保存首个原始错误；完整重复的Catch宏诊断保留在临时目录。

随后在独立CMake目录构建真实目标，C/C++编译器均为GCC10，Release/LTO/Werror、
preview/player/ABI关闭、tools/tests开启，仅共享依赖源码而不共享依赖对象目录。
构建 `nekomote_unit_tests` 与 `nekomote_anchor_audit` 成功。

```bash
LD_LIBRARY_PATH=/tmp/nekomote-gcc10-fix/root/usr/lib/x86_64-linux-gnu \
  /tmp/nekomote-gcc10-fix/build/bin/nekomote_unit_tests '~[integration]'
LD_LIBRARY_PATH=/tmp/nekomote-gcc10-fix/root/usr/lib/x86_64-linux-gnu \
  /tmp/nekomote-gcc10-fix/build/bin/nekomote_unit_tests '[runtime]'
```

- GCC10核心：242用例、54,014断言通过；runtime子集91用例、28,820断言通过。
- GCC15原Release配置：CPU目录267项无失败，3项既有跳过；runtime子集同样通过。
- GCC10与GCC15各运行 `nekomote_anchor_audit assets 360 sessions`，比较前轮提交的
  `anchor-evaluation-2026-11-10/sessions.jsonl`：150场景、54,000步及24次装载/切换计数
  （含初始安装）都一致。两份 comparison 的 canonical SHA256 同为
  `13c6cf9ea35f83c8f955a59547e639524be799657c03e94e22a3b0f5b9e0c80b`。
- 格式、源码diff和证据检查通过；未修改工作流、最低工具链、黄金数据或外部素材。

核心测试使用memcmp逐个float比较对象表示；工具的memcpy保留原uint32指纹字格式，
没有把正负零等价化，没有使用近似比较或为不同编译器添加分支。
