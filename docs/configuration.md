# 启动配置与面板偏好

配置分成两个 JSON 文件，均使用 `"version": 1`。资产目录只读，不设置默认资产位置，
也不把模型、图片或声音复制到用户配置目录。

启动配置保存后端、资产目录或显式模型路径、输出名称、输入模式、面板文件路径，
player 另使用窗口宽高。只有 `--save-config` 会写入启动配置，并在保存后直接退出，
无需显示服务器。普通运行先读取文件，再用本次命令行的显式参数覆盖。

面板文件保存用户选择的角色、背景、语言、帧率、静音、位置锁定、变换、图标和面板位置、背景渐变
时长。首次启动、自动适配、调整窗口尺寸和退出不会仅为保存默认值创建文件；用户操作
改变这些偏好时才写入。`--no-save` 允许读取，但禁止保存面板偏好。

## 路径与实例隔离

默认配置根目录为 `$XDG_CONFIG_HOME/nekomote/`；环境变量未设置或不是绝对路径时，
使用 `$HOME/.config/nekomote/`。读取不存在的文件只使用内存默认值，不创建目录。

| 入口 | 默认启动文件 | 默认面板文件 |
| --- | --- | --- |
| player | `player.json` | `player.panel.json` |
| X11 | `x11.json` | `x11.panel.json` |
| layer-shell | `layer-shell.json` | `layer-shell.panel.json` |
| Plasma 5 / 6 | 插件 KConfig 管理 | `plasma/<实例 UUID>.json` |
| 直接 C ABI / 通用 Qt 宿主 | 宿主管理 | `panel.json`，宿主可通过 C ABI 指定 |

独立入口用 `--config FILE` 指定启动文件。默认面板文件取同目录下
`<启动文件 stem>.panel.json`，也可用 `--state FILE` 单独指定并显式保存到启动配置。
输出名称只选择显示目标，不再派生配置标识。不同实例应选用不同启动文件和面板文件，
同一文件由一个运行实例维护；两个实例显式选择同一面板文件就会共享偏好。

默认派生路径不写进启动文件，复制 `left.json` 为 `right.json` 后会改用
`right.panel.json`。显式保存了自定义 `state_path` 的文件仍使用该指定路径。

Plasma 在每个壁纸配置中持久保存 UUID，区分不同桌面背景实例。资产目录、后端和 UUID
仍由插件 KConfig 管理，面板数据通过同一壁纸 C ABI 保存，不维护第二套面板业务。
手工复制 Plasma 的整份 KConfig 时应清空 `panelInstanceId`，使新实例生成自己的 UUID。

Plasma 5 / 6 的壁纸列表显示 **NekoMote Wallpaper**（简体中文：**NekoMote 壁纸**），
附带的简洁桌面布局显示 **NekoMote Desktop**（简体中文：**NekoMote 桌面**）。名称随
桌面语言选择；插件 ID 保持不变，已有壁纸配置继续使用。该布局只承载壁纸，不提供
文件夹视图的桌面图标功能；普通桌面布局也可以选择 NekoMote 壁纸。

壁纸设置中的资产目录应选择包含 `characters/` 的资产根目录，背景放在同级
`backgrounds/`。渲染后端只影响 NekoMote，应用更改时重建壁纸设备；Vulkan 需要驱动
支持 GPU 图像共享，无法加载时可改回默认 OpenGL。角色、背景、帧率和声音在壁纸上的
控制面板调整。设置页底部提供 [GitHub 仓库](https://github.com/Chocola-X/NekoMote) 与
[个人网站](https://www.nekopara.uk) 链接。

文件内相对路径相对于启动文件所在目录；命令行相对路径相对于本次工作目录，显式保存
时转换为绝对路径。启动文件与面板文件不得是同一路径。

## 使用

```bash
# 明确保存启动设置，不启动图形窗口；不会创建面板文件
nekomote_x11 --config ~/.config/nekomote/left.json \
  --assets /mnt/wallpapers/nekomote-assets --backend vulkan --output DP-1 --save-config

# 恢复保存的设置；面板操作只修改 left.panel.json
nekomote_x11 --config ~/.config/nekomote/left.json

# 第二个独立实例
nekomote_layer_shell --config ~/.config/nekomote/right.json \
  --assets /mnt/wallpapers/nekomote-assets --output DP-2 --save-config
nekomote_layer_shell --config ~/.config/nekomote/right.json

# 默认实例：先显式保存，之后可以无参数启动
nekomote_player --assets /mnt/wallpapers/nekomote-assets --backend vulkan --save-config
nekomote_player
```

首次没有启动配置、也没有传入资产或模型路径时明确报错，不猜测或创建资产目录。
`--mute`、`--ui-open`、截图和固定帧等参数只作用于本次运行；`--save-config` 不接受这些
临时选项或 `--no-save`。`--help` 和壳的 `--list-outputs` 不读取启动配置。

启动文件示例：

```json
{
  "version": 1,
  "assets_dir": "/mnt/wallpapers/nekomote-assets",
  "backend": "vulkan",
  "output": "DP-1",
  "input": "interactive",
  "state_path": "/home/user/.config/nekomote/left.panel.json"
}
```

面板文件最小示例：

```json
{
  "version": 1,
  "character": "azuki/casual",
  "language": "zh-CN",
  "fps": 60,
  "mute": true
}
```

完整面板字段由共享应用层定义，尺寸不属于面板配置。损坏 JSON、未知版本、未知字段或
无效类型会报告带路径的错误，不覆盖原文件或静默改用其他位置。写入使用同目录独占
临时文件，完成后原子替换，避免中断造成半份 JSON；写入失败会显式报告。
`lock_transform` 表示锁定角色位置和大小；面板展开、临时暂停及宿主可用性不作为偏好保存。

## 旧配置迁移

运行时不再读取 `assets/config.json` 或旧的按输出名称派生的面板文件。需要保留旧偏好时，
执行一次离线迁移；目标文件必须不存在，源文件保持不变：

```bash
python3 tools/migrate_wallpaper_config.py \
  --source /path/to/assets/config.json \
  --output ~/.config/nekomote/left.panel.json
```

该工具只迁移面板偏好，移除原有 `width` / `height`，不会推定资产目录或启动后端。
再用 `--save-config` 明确保存启动设置。Plasma 的面板路径可从插件的实例 UUID 确定，
迁移时应先停止对应实例，避免运行中的面板写回覆盖迁移结果。
