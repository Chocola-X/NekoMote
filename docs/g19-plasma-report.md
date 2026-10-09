# G19：Plasma 包配置、生命周期与部署验证

基线 `1eea941`（Codex 整合提交）。本任务扩展实际 KPackage 包探针，补齐配置页/schema 加载、配置变更重建、生命周期、错误恢复和部署清单。不修改求值/GPU 公式/ABI 签名。

## 1. 构建

```bash
cmake --preset plasma && cmake --build --preset plasma
```

Release/WERROR，Qt 生成代码 `-Wno-missing-declarations` 例外不扩大到正式源码。

## 2. 资源清单

| 产物 | 路径 | 用途 |
| --- | --- | --- |
| 动态库 | `lib/libnekomote_plasma_qml.so` | NekoWallpaperItem C++ 实现 |
| QML 插件 | `adapters/plasma/NekoMote/Wallpaper/libnekomote_plasma_qmlplugin.so` | QML import NekoMote.Wallpaper |
| QML 类型 | `adapters/plasma/NekoMote/Wallpaper/nekomote_plasma_qml.qmltypes` | QML 类型注册 |
| KPackage | `adapters/plasma/wallpapers/org.nekomote.wallpaper/` | Plasma/Wallpaper 包 |
| 包 main.qml | `…/contents/ui/main.qml` | 壁纸主 QML（Loader + 配置绑定） |
| 包 config.qml | `…/contents/ui/config.qml` | 配置页（文件选择 + 范围限制） |
| 包 schema | `…/contents/config/main.xml` | KConfig schema |
| 包 metadata | `…/metadata.json` | KPackage 元数据 |
| Containment | `adapters/plasma/probe/containment/` | 隔离验证 containment |
| 包探针 | `bin/nekomote_plasma_package_probe` | 实际 KPackage 诊断工具 |
| QML 探针 | `bin/nekomote_plasma_probe` | 隔离 QML 组件诊断 |
| 安全测试 | `bin/nekomote_plasma_item_safety` | 四安全负例 + 生命周期 |
| 配置测试 | `bin/nekomote_plasma_config_reject` | 配置拒绝合成测试 |

不复制整个 build 目录；不让旧 STATIC 产物混入。

## 3. 可重复暂存命令

`nekomote_plasma_package_probe` 自动暂存到 workspace 临时 XDG：

```bash
# 截图模式
build/plasma/bin/nekomote_plasma_package_probe \
  --model assets/characters/chocola-date-a/model.nkm.json \
  --session assets/characters/chocola-date-a/session.nks.json \
  --output /tmp/g19-output

# 生命周期测试（20 轮创建/删除 + 20 轮配置切换）
build/plasma/bin/nekomote_plasma_package_probe \
  --model assets/characters/chocola-date-a/model.nkm.json \
  --session assets/characters/chocola-date-a/session.nks.json \
  --output /tmp/g19-lifecycle --lifecycle

# 坏资产测试
build/plasma/bin/nekomote_plasma_package_probe \
  --output /tmp/g19-bad --bad-asset

# 非法配置测试
build/plasma/bin/nekomote_plasma_package_probe \
  --output /tmp/g19-invalid --invalid-config

# DPI 测试
build/plasma/bin/nekomote_plasma_package_probe \
  --model assets/characters/chocola-date-a/model.nkm.json \
  --session assets/characters/chocola-date-a/session.nks.json \
  --output /tmp/g19-dpi --dpi 1.5
```

暂存仅 workspace 临时 XDG_DATA_HOME/CONFIG_HOME/CACHE_HOME，不改正在运行的桌面、不安装用户插件、不重启 shell。

### 可审查安装/卸载命令（不自动执行）

```bash
# 安装到用户目录
kpackagetool6 --type Plasma/Wallpaper --install \
  build/plasma/adapters/plasma/wallpapers/org.nekomote.wallpaper

# 卸载
kpackagetool6 --type Plasma/Wallpaper --remove org.nekomote.wallpaper
```

## 4. 配置界面

config.qml 改进：
- 文件选择对话框（FileDialog + 浏览按钮）
- URL 格式校验提示（非 file:/// 开头时显示警告）
- originX/originY 范围 -8192..8192
- scale 范围 10%..300%
- 明确标签和布局
- 不暴露内部步骤计数/ABI 字段

## 5. 生命周期测试结果

模型：`assets/characters/chocola-date-a/`

| 测试 | 结果 |
| --- | --- |
| pause/resume | 暂停不推进帧，恢复首 delta≈0.033（一帧间隔，不追赶 500ms 暂停时间） |
| hide/show | 隐藏后无新帧，显示后恢复 |
| resize 640x480 | 帧推进，尺寸跟随 |
| 创建/删除 20 轮 | 每轮改 originX 触发 Loader 重建，renderer_created=1/轮 |
| 配置切换 20 轮 | 每轮改 scale 触发 Loader 重建，renderer_created=1/轮 |
| DPI 1.5 | 截图成功 |

每轮记录 renderer_created 计数。配置变更通过 KConfigPropertyMap setProperty 触发 QML 属性变化 → reloadSession → Loader 重建。

## 6. 错误恢复测试

| 模式 | native status | errorString | GUI |
| --- | --- | --- | --- |
| 坏资产（不存在文件） | 2 | "load model: asset load failed" | 存活，截图成功 |
| 非法配置（坏 JSON） | 2 | "load model: asset load failed" | 存活，截图成功 |

## 7. 合成测试

`plasma_config_reject`（10 项，无窗口）：
- 空配置 status=0
- 仅 model / 仅 session status=0
- 缺失文件 status=2
- 坏 JSON status=2
- scale=0 / scale=-1 status=2
- originX=NaN / originY=Infinity status=2
- 输入隔离（diagnostic 模式禁用 hover/mouse）

`plasma_item_safety`（四安全负例 + 生命周期）：保留不退化。

## 8. 验证

- plasma 148/148 通过（含新增 plasma_config_reject）
- dev 145/145 通过
- 三固定 QML/WebGL 门槛不退化（plasma_item_safety 保留）
- 不宣称已修改真实桌面

## 9. 二进制 hash

```
c9839345…  libnekomote_plasma_qml.so
91984ed9…  libnekomote_plasma_qmlplugin.so
e5986123…  nekomote_plasma_package_probe
2f4379b2…  nekomote_plasma_probe
```
