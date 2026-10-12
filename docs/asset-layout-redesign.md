# 资产目录重组设计

本文件记录资产目录结构重组的完整设计。所有决策已敲定，待实现。

## 1. 背景与问题

当前 `assets/` 目录结构存在以下问题（证据见 2026-10-12 架构核查）：

| 问题 | 证据 |
| --- | --- |
| 角色目录扁平 195 个，命名 `<角色>-<服装>-<变体...>` 混乱 | `assets/characters/` 下 195 个子目录 |
| 角色显示名靠硬编码映射表 + 前缀匹配 hack | `src/application/catalog.cpp:61-92`（8 组+17 服装+11 变体）、`catalog.cpp:65-169`（parse_display_name） |
| 菜单框架文本硬编码且在两处重复 | `src/abi/wallpaper_instance.cpp:852-1062` 与 `apps/player/main.cpp:1165-1401` |
| 无顶层资产清单文件 | `assets/` 顶层只有 `config.json`（运行配置） |
| `meta.json` 仅读 1 字段 | `catalog.cpp:36-56` 只取 `cutoff_y` |
| 动态背景按文件名末尾 A/B/C 硬编码分组，轮换规则在代码里 | `catalog.cpp:235-270`，`BackgroundEntry` 硬编码 `path_a/b/c`（`catalog.hpp:54-56`） |
| 图标文件名 `neko.png` 硬编码两处 | `wallpaper_instance.cpp:746`、`player/main.cpp:819` |

## 2. 目标目录结构

```
assets/
├── characters/
│   ├── characters.json              ← 可选，路径→显示名完整翻译字典
│   ├── <角色名>/
│   │   └── <服装变体名>/            ← 叶子目录，含一个模型的全部文件
│   │       ├── model.nkm.json
│   │       ├── session.nks.json
│   │       ├── meta.json            ← 实例级元数据
│   │       ├── textures/
│   │       ├── audio/
│   │       └── ...
│   └── ...
├── backgrounds/
│   ├── background.json              ← 可选，静态背景文件名→显示名映射
│   ├── *.png                        ← 静态背景图片
│   └── dynamic/
│       ├── background.json          ← 可选，组名→显示名映射
│       └── <组名>/                  ← 每组动态背景一个目录
│           ├── config.json          ← 必须，定义 N 相轮换规则
│           └── *.png                ← 各相图片
├── icons/
│   └── toggle.png                   ← 面板开关图标，缺失回落矢量绘制
└── config.json                      ← 运行配置（当前角色/背景/缩放/fps/mute）
```

### 2.1 角色目录：两层

```
characters/<角色名>/<服装变体名>/{model.nkm.json, meta.json, ...}
```

- 第一层 = 角色名（如 `vanilla`、`azuki`、`milk`），均为不含 `-` 的单词
- 第二层 = 服装变体名，可含多段（如 `casual`、`casual-a`、`casual-b-no-bell-tail-pin`、`teenage-4yo`）
- 叶子是模型文件，不是再嵌套目录
- 无变体的模型第二层就是服装名（如 `azuki/casual/`）

**切分规则**：原扁平目录名在第一个 `-` 处切分，第一段为角色名，剩余为服装变体名。已验证全部 195 个目录第一段均为已知角色名之一（`chocola`/`vanilla`/`azuki`/`coconut`/`maple`/`cinnamon`/`milk`/`fraise`），切分零歧义。

### 2.2 为什么两层而非三层

- 迁移零歧义：第一个 `-` 即角色名边界，不需猜测服装/变体边界
- 匹配 UI 两级菜单：当前 UI 就是"角色组抽屉 → 角色项"两级
- 消除 `parse_display_name` 的角色名切分逻辑（`catalog.cpp:65-169` 最复杂部分可删除）
- 服装与变体的区分只在显示名，不在目录结构

## 3. 各文件 Schema

### 3.1 `characters/<角色名>/<服装变体名>/meta.json`（实例级元数据）

```json
{
  "cutoff_y": 1042.91,
  "display_name": "日常·B·无铃铛·尾部固定",
  "hidden": false,
  "order": 10
}
```

| 字段 | 类型 | 必需 | 说明 |
| --- | --- | --- | --- |
| `cutoff_y` | float | 否 | 底部裁剪基准，当前已有 |
| `display_name` | string | 否 | 覆盖自动解析的显示名，最高优先级 |
| `hidden` | bool | 否 | `true` 则不进菜单（调试用角色可隐藏），默认 `false` |
| `order` | int | 否 | 组内排序权重，小的在前，默认按目录名字典序 |

缺失的模型走默认值，`meta.json` 整个文件不存在也合法。

### 3.2 `characters/characters.json`（可选，显示名翻译字典）

```json
{
  "vanilla/casual": "日常",
  "vanilla/casual-a": "日常·A",
  "vanilla/casual-b-no-bell-tail-pin": "日常·B·无铃铛·尾部固定",
  "azuki/casual": "日常",
  "milk/teenage-4yo": "少女·4岁版"
}
```

- key = 两层相对路径（`角色名/服装变体名`），用 `/` 分隔
- value = 显示名字符串（当前只 `zh-CN`，不做多语言；多语言是后续独立任务）
- **纯查表，无解析逻辑**：扫描时直接用路径查 key
- 文件可选：缺失则回落到第二层目录名原样作为显示名

### 3.3 显示名解析优先级

```
1. meta.json 的 display_name（若存在）
2. characters.json 查表（若文件存在且 key 命中）
3. 回落：第二层目录名原样
```

三层各自独立可选，任一缺失不影响其余。

### 3.4 `backgrounds/background.json`（可选，静态背景显示名）

升级自当前 `background-names.json`，schema 保持一致（当前只取 `zh-CN`）：

```json
{
  "BG_07A.png": {
    "zh-CN": "海滨餐厅露台",
    "ja": "海辺のレストランテラス"
  }
}
```

- 文件名从 `background-names.json` 改为 `background.json`
- 可选：缺失则回落到图片文件名 stem
- 当前 `catalog.cpp:192-213` 的加载逻辑改为读 `background.json`，其余不变

### 3.5 `backgrounds/dynamic/<组名>/config.json`（必须，N 相轮换规则）

```json
{
  "phases": [
    { "image": "A.png", "start": "06:00" },
    { "image": "B.png", "start": "12:00" },
    { "image": "C.png", "start": "18:00" }
  ],
  "transition_seconds": 1.8
}
```

| 字段 | 类型 | 必需 | 说明 |
| --- | --- | --- | --- |
| `phases` | array | 是 | 各相图片及起始时间，长度 N ≥ 1 |
| `phases[].image` | string | 是 | 图片文件名（相对组目录） |
| `phases[].start` | string | 是 | 该相起始时间 `HH:MM`（24 小时制），按时间排序后循环切换 |
| `transition_seconds` | float | 否 | 相间渐变时间，默认 1.8 |

- **必须存在**：每组动态背景目录下必须有 `config.json`
- **N 相可变**：不限于三相，支持昼夜四相、季节五相等
- `start` 时间按升序排列，最后一个 phase 的 `start` 到次日第一个 phase 的 `start` 为其持续时间
- 当前硬编码的"时钟分相 A/B/C + 1.8s 渐变"（`catalog.cpp:235-270`）由此文件取代

### 3.6 `backgrounds/dynamic/background.json`（可选，动态组显示名）

```json
{
  "BG_01": "街角法式糕点店",
  "BG_02": "糕点店内的展示柜台"
}
```

- key = 组目录名，value = 显示名
- 可选：缺失则回落到组目录名原样
- 与 `characters/characters.json` 对称

### 3.7 `assets/config.json`（运行配置，保持职责不变）

当前 schema 不变（`catalog.hpp:91-105`），唯一变更：

| 字段 | 变更前 | 变更后 |
| --- | --- | --- |
| `character` | 扁平目录名 `"azuki-casual-a"` | 两层路径 `"azuki/casual-a"` |
| `background` | 不变 | 不变 |

旧 `config.json` 由迁移工具一次性转换。

## 4. 图标方案

### 4.1 单一图标

当前只有一个控制图标：面板 toggle 按钮。重组后：

```
assets/icons/toggle.png
```

- 文件名从 `neko.png` 改为 `toggle.png`（语义清晰）
- 代码侧硬编码标准名 `"toggle.png"`（唯一图标，不需 enum/映射表）
- 加载：`assets/icons/toggle.png` 存在则 `load_png` + `upload_icon`，缺失则 `texture_id = 0`
- 缺失回落：`src/ui/layout.cpp:108-115` 已有矢量 fallback（画简化圆角方块+横线），保持不变

### 4.2 为什么不放配置文件

图标用途是 UI 结构决定的（固定），文件名是约定不是配置。换皮肤只需替换 `icons/toggle.png`，不改代码、不改配置。

### 4.3 UI 文本保持写死

菜单框架文本（sidebar 标题、section 名、开关/按钮标签、帧率选项、署名）**保持硬编码在代码里**，不做配置化。这些文本随 UI 构建逻辑一起抽到共享位置（见下文第一步），但来源是代码常量不是配置文件。

## 5. 实现计划

分两步，第一步纯重构消除 UI 重复，第二步引入资产清单 + 目录重组。

### 5.1 第一步：消除 UI 构建重复

**目标**：player 和 Plasma 走同一个菜单构建函数，文本只在一处。

**新增文件**：
- `include/nekomote/ui/menu.hpp`
- `src/ui/menu.cpp`
  - `void build_sidebar(ui::Context&, const application::AssetCatalog&, const application::WallpaperConfig&, MenuState&)`
  - 内部调用 `ui::layout` 原语（`drawer_header`/`section_header`/`radio_row`/`switch_row` 等）
  - 文本硬编码在此文件（从 `wallpaper_instance.cpp` 搬过来），只一份

**修改文件**：
- `src/abi/wallpaper_instance.cpp`：`draw_ui()` 改为调 `ui::build_sidebar(...)`，删除 852-1062 的内联构建
- `apps/player/main.cpp`：删除 1165-1401 的内联构建，改为调 `ui::build_sidebar(...)`
- `src/ui/CMakeLists.txt`：加 `menu.cpp`

**设置抽屉 index**：当前硬编码 `expanded_drawer == 8`（`wallpaper_instance.cpp:887/939/942`）改为 `expanded_drawer == character_groups.size()`，动态计算，避免角色组数量变化错位。

**验证**：
- player 启动后菜单文本与之前逐字一致
- Plasma 壁纸菜单文本与之前一致
- `MenuState` 命中测试、expanded_drawer、单选/开关状态行为不变

### 5.2 第二步：目录重组 + 资产清单 + 配置驱动

**目标**：扁平 195 目录转两层，引入 characters.json/background.json/dynamic config.json，显示名和轮换规则从资产读取。

**迁移工具**：importer 子命令 `nekomote-import migrate-layout`

```
nekomote-import migrate-layout --assets <assets_dir>
```

工具执行：
1. 角色目录重组：遍历 `characters/` 下扁平目录，按第一个 `-` 切分，移动到 `<角色名>/<服装变体名>/`
2. 生成 `characters/characters.json`：对每个模型，用当前 `parse_display_name` 逻辑计算显示名，写入字典
3. 背景重命名：`background-names.json` → `background.json`
4. 动态背景重组：`dynamic/` 下按文件名末尾 A/B/C 分组，每组创建 `<组名>/` 目录，移入图片，生成 `config.json`（默认三相 06:00/12:00/18:00 + 1.8s）
5. 生成 `dynamic/background.json`：从原 `background-names.json` 提取动态组显示名
6. 图标重命名：`icons/neko.png` → `icons/toggle.png`
7. 迁移 `config.json`：`character` 字段从扁平名转为两层路径

**代码修改**：
- `include/nekomote/application/catalog.hpp`：
  - `CharacterEntry` 的 `name` 改存两层路径（`角色名/服装变体名`）
  - `BackgroundEntry` 的 `path_a/path_b/path_c` 改为 `std::vector<std::filesystem::path> phases` + 轮换元数据
  - `path_for_phase()` 泛化为按时间查 phase
- `src/application/catalog.cpp`：
  - `scan()` 角色部分：遍历两层目录 `characters/<角色>/<服装变体>/`，检查 `model.nkm.json`
  - 删除 `parse_display_name`（`catalog.cpp:65-169`）和硬编码映射表（`catalog.cpp:61-92`）
  - 显示名：读 `meta.json` 的 `display_name` → 查 `characters.json` → 回落第二层目录名
  - 背景部分：读 `background.json`（非 `background-names.json`）
  - 动态背景：遍历 `dynamic/<组>/`，读 `config.json`，按 `phases` 构建
  - `find_character()` 按两层路径匹配
- `src/abi/wallpaper_instance.cpp`：图标路径 `"neko.png"` → `"toggle.png"`
- `apps/player/main.cpp`：同上
- `src/ui/menu.cpp`（第一步已建）：帧率选项等文本保持硬编码

**验证**：
- 迁移后 195 个角色全部可扫描、显示名与迁移前一致（冻结迁移前显示名作为基准逐个对比）
- 删除 `characters.json` 后显示名回落到第二层目录名
- `meta.json` 加 `display_name` 后该角色显示名变化
- `hidden: true` 的角色不出现
- 动态背景 N 相轮换按 `config.json` 的 `start` 时间切换
- 图标 `toggle.png` 正常显示，删除后回落矢量绘制
- `config.json` 的 `character` 字段路径形式可正确查找

## 6. 受影响文件清单

| 文件 | 第一步 | 第二步 |
| --- | --- | --- |
| `include/nekomote/ui/menu.hpp` | 新增 | — |
| `src/ui/menu.cpp` | 新增 | — |
| `src/ui/CMakeLists.txt` | 改 | — |
| `src/abi/wallpaper_instance.cpp` | 改：draw_ui 调 build_sidebar | 改：图标路径 |
| `apps/player/main.cpp` | 改：删内联 UI | 改：图标路径 |
| `include/nekomote/application/catalog.hpp` | — | 改：CharacterEntry/BackgroundEntry 结构 |
| `src/application/catalog.cpp` | — | 改：scan + 删 parse_display_name + 显示名解析 |
| `tools/import_emote/` | — | 改：加 migrate-layout 子命令 |
| `assets/characters/` | — | 重组为两层 |
| `assets/characters/characters.json` | — | 新增 |
| `assets/backgrounds/background-names.json` | — | 重命名为 background.json |
| `assets/backgrounds/dynamic/` | — | 重组为每组一目录 + config.json |
| `assets/backgrounds/dynamic/background.json` | — | 新增 |
| `assets/icons/neko.png` | — | 重命名为 toggle.png |
| `assets/config.json` | — | character 字段转路径形式 |

## 7. 回退策略

- 第一步独立可回退（git revert，无资产文件变更）
- 第二步：迁移工具在迁移前备份原结构到 `assets/.backup-pre-migrate/`，可回滚
- `characters.json`/`background.json`/`dynamic/background.json` 删除即回落到扫描驱动默认
- `meta.json` 扩展是可选的，不提供新字段的角色走默认值

## 8. 不做什么

- **不在 characters.json 里做原子段拆解翻译**：每个组合完整翻译，纯查表无解析逻辑
- **不做完整 i18n 框架**：显示名当前只 `zh-CN`，多语言是后续独立任务
- **不把 config.json（运行配置）合并进 characters.json**：两者职责不同，保持分离
- **不把菜单文本配置化**：UI 文本保持硬编码在代码里，只有图标和显示名从资产读取
- **不把图标做成配置驱动**：图标文件名是约定（`toggle.png`），不放配置文件
- **不为动态背景保留旧扁平格式兼容**：迁移工具一次性转换，旧格式明确拒绝（AGENTS.md §2）
- **不做可变深度角色目录**：固定两层，不递归
