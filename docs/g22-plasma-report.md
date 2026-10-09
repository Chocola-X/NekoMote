# G22：实际 Plasma 包剩余功能与部署

基线 `920245e`（单边界继承整合）。G19 的初态/config/首delta0/40轮销毁已由 Codex 修，本轮不重复。包 probe 是预链接 native 的实际 KDE 宿主，不能据此宣布干净动态部署已验证。

## 完成项

### 1. bad→good→bad→good 恢复（G22-1）

`--recovery` 模式验证有效 model/session 改回后动作继续推进：

- 2 轮 model 切换：bad→good→bad→good，每轮旧 state renderer created==destroyed，新 status=1，帧推进
- session 切换：bad→good，旧 state 销毁平衡
- originY/scale 切换：Loader 重建，status=1，帧推进
- 非本地 URL（`http://`）到达 error 分支：`model/session URL must be a local file`
- scale=0 到达 error 分支；originY=NaN 到达 error 分支
- 批量更新（originY+scale 同时设）只重建 1 次（`reloadQueued` + `Qt.callLater` 合并）

修改 `wallpaper_item.cpp` setter：`state_` 已存在时静默更新值而非报错，因 QML Loader 绑定会先更新旧 item 再重建。`configure_diagnostics` 分离输入隔离（任意时刻可用）与固定步长（渲染前配置）。

### 2. 自动实际包入口 CTest（G22-2）

`tests/plasma/test_package_probe.py` 注册为 CTest `plasma_package_probe`（GPU flock）：

| 测试 | 模式 | 耗时 | peak RSS |
| --- | --- | --- | --- |
| bad_asset | `--bad-asset` | 0.82s | 220MB |
| invalid_config | `--invalid-config` | 0.79s | — |
| capture | 截图 | 1.08s | 89MB |
| lifecycle | `--lifecycle --rounds 5` | 4.90s | 75MB |
| lifecycle_dpi_1_5 | `--dpi 1.5` | 3.91s | 3MB |
| recovery | `--recovery` | 3.70s | — |

- 9 默认配置拒绝（plasma_config_reject）：empty/only-model/only-session/missing/bad-JSON/scale-0/scale-neg/originX-NaN/originY-inf
- 1 可选输入隔离（需 NEKOMOTE_TEST_MODEL/SESSION）
- 四安全负例：bad-asset/invalid-config/missing-files/non-local-URL
- 每轮 Loader 重建后 `configure_diagnostics` 隔离 hover/mouse 并验证
- 首帧 delta0 通过 `frame_ready` 信号捕获，非轮询最后一帧
- resize containment（`item->size()`）和 FBO 尺寸（`frame_ready` framebuffer_size）双重断言
- DPI 1 和 1.5 均验证
- CPU peak RSS 与 renderer created/destroyed 计数分开报告

### 3. 干净可部署 stage（G22-3）

CMake target `nekomote_plasma_stage` / `nekomote_plasma_stage_clean` 生成自包含 stage：

```
stage/
  lib/libnekomote_plasma_qml.so
  qml/NekoMote/Wallpaper/{qmldir,qmltypes,plugin.so}
  package/org.nekomote.wallpaper/{metadata,contents/ui/main.qml,config.qml,config/main.xml}
  package/org.nekomote.validation/{metadata,contents/ui/main.qml}
  manifest.json
```

- 10 必需文件全部存在
- native 库用 `LD_LIBRARY_PATH=stage/lib` 从 stage 加载（非 build RPATH）
- plugin RUNPATH 仍指向 build tree（`patchelf` 不可用），运行时需 `LD_LIBRARY_PATH` 覆盖
- 静态核心 368 nekomote 符号编入 DSO（`nekomote_plasma_qml.so` 静态链接 model/runtime/asset_io/application/render_gl/abi）
- `tools/plasma_stage_verify.py` 验证 stage 自包含并生成 manifest

### 4. 可审查安装/卸载（G22-4）

`tools/plasma_install.sh --install|--uninstall|--status`：

- 安装到 `~/.local/share/plasma/`（用户 XDG），不安装系统
- 不改真实桌面设置、不重启 shell、不复制 PCM/音频服务
- 安装日志记录 stage hash、文件数、时间戳
- `--status` 查看安装记录；`--uninstall` 清除并报告

## 验证

- Release/WERROR 构建通过（plasma preset，无新增警告）
- 141 单元测试 / 37694 assertions 全通过
- plasma_config_reject：9 默认 + 1 可选（SKIPPED 无素材时）
- plasma_item_safety：4 状态 + playback 生命周期全通过
- recovery：16 步 bad→good 循环 + 批量重建 = 1 全通过
- lifecycle：10+10 轮 create/destroy + config switch，每轮诊断隔离，renderer 1/1 平衡
- lifecycle DPI 1.5：5+5 轮通过
- teardown renderer created==destroyed 全平衡

## 证据

`build/evidence/g22-plasma/`：
- `unit-tests.log` — 141 cases / 37694 assertions
- `config-reject.log` — 9 默认 + 1 SKIPPED
- `item-safety.log` — 4 状态 + playback
- `recovery.log` — 16 步恢复 + 批量重建
- `lifecycle.log` — 10+10 轮 + FBO 尺寸断言
- `lifecycle-dpi15.log` — DPI 1.5
- `stage-manifest.json` — stage 自包含验证
- `source-hashes.txt` — 修改文件 hash
- 截图：recovery_final.png / lifecycle_final.png / dpi15 lifecycle_final.png

## 已知限制

- plugin RUNPATH 指向 build tree（`patchelf` 不可用），运行时需 `LD_LIBRARY_PATH=stage/lib`
- 正式声音/背景服务保持共享边界，未在本轮接入
- 正式应用接入（非验证 probe）另安排
- 长期运行/多屏/真实桌面安装未验证
- 40 轮后续 Loader 恢复真实输入已通过每轮 `configure_diagnostics` 隔离验证，但未测 8-24h 稳态

## 修改文件

- `adapters/plasma/wallpaper_item.cpp` — setter 静默更新 + configure_diagnostics 分离
- `adapters/plasma/probe/package_main.cpp` — `--recovery` 模式 + FBO 尺寸断言 + 每轮诊断隔离
- `adapters/plasma/CMakeLists.txt` — stage target
- `tools/plasma_stage_verify.py` — stage 验证
- `tools/plasma_install.sh` — 安装/卸载
- `tests/plasma/test_package_probe.py` — CTest 入口
- `CMakeLists.txt` — CTest 注册
