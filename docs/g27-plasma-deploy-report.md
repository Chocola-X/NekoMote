# G27：完整、可搬移的 Plasma 部署

基线：Codex G22/G24/G26 复核提交（`3bb5023`）。本任务由 GLM-5.2 执行。

## 交付内容

### G27-1: CMake install() 目标 + RPATH + stage manifest

- `adapters/plasma/CMakeLists.txt`：添加完整 `install()` 目标（native 库、QML qmldir/qmltypes/plugin、KPackage wallpaper + validation containment），组件 `plasma`
- QML plugin `INSTALL_RPATH` 设为 `$ORIGIN/../../../lib`（相对路径，不指向 build tree）
- stage 目标改为 `cmake --install --prefix <stage> --component plasma`，自动处理 RPATH 替换
- 新增 `tools/plasma_stage_manifest.py`：生成 stage manifest.json（文件 hash + ELF RUNPATH/NEEDED）
- `CMakePresets.json`：添加 plasma testPreset

验证：stage 中 plugin RUNPATH=`$ORIGIN/../../../lib`，`ldd` 不需 `LD_LIBRARY_PATH` 即可解析 native 库。

### G27-2: install/uninstall/status 工具完整重写

- 新增 `tools/plasma_deploy.py`（替代旧 `plasma_install.sh`）
- 安装所有组件：KPackage + QML 模块 + native 库 + env.sh
- 遵循 `XDG_DATA_HOME`（默认 `~/.local/share`），支持显式 `--prefix`
- 非法 prefix 拒绝（`/`、`/usr`、`/etc` 等系统路径、相对路径）
- 安装记录 `.install-record.json`：每个文件 sha256 + size
- 卸载只删除安装记录中的文件，保留用户无关内容
- `--verify`：校验所有文件 hash

验证：临时根 install→status→verify→uninstall→无残留；无关文件保留；负例 prefix 拒绝。

### G27-3: verifier 增强

- 重写 `tools/plasma_stage_verify.py`：6 项检查（files/manifest/rpath/ldd/symbols/smoke），任一失败非零退出
- 不依赖 `LD_LIBRARY_PATH`：smoke test 仅用 `QML_IMPORT_PATH` 指向 stage
- readelf 检查 RUNPATH 不指向 build tree（FAIL 不只是 WARN）
- 4 个负例：`missing-lib`、`missing-plugin`、`corrupted-manifest`、`no-screenshot`，均正确失败

### G27-4: 测试修复

- `tests/plasma/test_package_probe.py`：probe 路径和证据目录通过 CMake 环境变量传入（`NEKOMOTE_PLASMA_PROBE`、`NEKOMOTE_EVIDENCE_DIR`），不硬编码 `build/plasma`
- RSS 改为单进程测量（`/proc/<pid>/status` VmHWM），不用 `RUSAGE_CHILDREN` 累计差值
- 证据保存到 `build/evidence/plasma-tests/`，tearDown 不删除
- `adapters/plasma/probe/package_main.cpp`：recovery 循环中 good 旧 state 引用修复（`old_state` 用于验证 good 实例 renderer 平衡）；probe 支持运行时 `QML_IMPORT_PATH`（不覆盖已设值）

### G27-5: 独立动态宿主验证

- 新增 `tools/plasma_deploy_verify.py`：6 步完整验证
  1. stage verify（所有检查通过）
  2. install 到临时 XDG 根
  3. status 确认安装
  4. 动态加载（probe 不设 `LD_LIBRARY_PATH`，`QML_IMPORT_PATH` 指向安装位置）：lifecycle + recovery 通过
  5. uninstall
  6. 无残留
- 证据保存到 `build/evidence/g27-deploy-verify/`

## 验证结果

- Release/WERROR plasma 构建：通过
- 151 测试全部通过（含 3 plasma CTest）
- stage verify + 4 负例：全部正确
- 独立动态宿主验证：6 步全部通过
- plugin RUNPATH：`$ORIGIN/../../../lib`（不指向 build tree）
- native 库通过相对 RPATH 加载，不需 `LD_LIBRARY_PATH`

## 尚未验证

- 真实桌面登录会话测试（需用户安排）
- 长期运行和多屏
- 跨小版本兼容性（仅 Plasma 6.3.6 + Qt 6.9.1 单点验证）

## 改动文件

- `adapters/plasma/CMakeLists.txt`：install() + RPATH + stage
- `adapters/plasma/probe/package_main.cpp`：recovery 旧 state + 运行时 QML 路径
- `CMakeLists.txt`：测试环境变量传入
- `CMakePresets.json`：plasma testPreset
- `tools/plasma_deploy.py`：新安装工具
- `tools/plasma_stage_manifest.py`：新 manifest 生成
- `tools/plasma_stage_verify.py`：增强验证器
- `tools/plasma_deploy_verify.py`：新独立验证
- `tests/plasma/test_package_probe.py`：测试修复
- 删除 `tools/plasma_install.sh`
