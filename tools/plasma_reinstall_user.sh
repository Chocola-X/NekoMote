#!/bin/bash
# NekoMote Plasma 壁纸插件重装脚本（清除旧安装 + 安装新版本）
#
# 用法:
#   ./plasma_reinstall_user.sh [--assets-dir DIR] [--stage DIR] [--prefix DIR]
#
# 默认 stage:  build/dev/adapters/plasma/stage
# 默认 prefix: $XDG_DATA_HOME (或 ~/.local/share)
#
# 本脚本只安装到用户目录（$XDG_DATA_HOME），不修改系统路径、不触碰当前桌面背景。
# 测试请在独立用户账户中进行。

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

STAGE_DIR="${REPO_DIR}/build/dev/adapters/plasma/stage"
PREFIX_DIR=""
ASSETS_DIR=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --stage)       STAGE_DIR="$2"; shift 2 ;;
        --prefix)      PREFIX_DIR="$2"; shift 2 ;;
        --assets-dir)  ASSETS_DIR="$2"; shift 2 ;;
        --help|-h)
            echo "用法: $0 [--assets-dir DIR] [--stage DIR] [--prefix DIR]"
            echo ""
            echo "  --assets-dir  assets 目录路径 (默认: \$REPO/assets)"
            echo "  --stage       stage 目录 (默认: build/dev/adapters/plasma/stage)"
            echo "  --prefix      安装前缀 (默认: \$XDG_DATA_HOME 或 ~/.local/share)"
            exit 0 ;;
        *) echo "未知选项: $1"; exit 1 ;;
    esac
done

if [[ -z "$PREFIX_DIR" ]]; then
    if [[ -n "${XDG_DATA_HOME:-}" ]]; then
        PREFIX_DIR="$XDG_DATA_HOME"
    else
        PREFIX_DIR="$HOME/.local/share"
    fi
fi

if [[ -z "$ASSETS_DIR" ]]; then
    if [[ -d "${REPO_DIR}/assets" ]]; then
        ASSETS_DIR="${REPO_DIR}/assets"
    fi
fi

echo "=== NekoMote Plasma 壁纸插件重装 ==="
echo "  stage:  $STAGE_DIR"
echo "  prefix: $PREFIX_DIR"
echo "  assets: ${ASSETS_DIR:-未指定}"
echo ""

# ============================================================
# 第一步：清除旧安装
# ============================================================
echo "--- 1/3 清除旧安装 ---"

# 1a. 用 plasma_deploy.py 卸载（基于安装记录逐文件删除）
if python3 "$SCRIPT_DIR/plasma_deploy.py" --uninstall --prefix "$PREFIX_DIR" 2>/dev/null; then
    echo "  安装记录卸载完成"
else
    echo "  无安装记录或卸载失败，继续手动清除"
fi

# 1b. 手动清除残留目录（以防记录丢失或旧脚本安装路径不同）
RESIDUAL_DIRS=(
    "$PREFIX_DIR/nekomote"
    "$PREFIX_DIR/plasma/wallpapers/org.nekomote.wallpaper"
    "$PREFIX_DIR/plasma/plasmoids/org.nekomote.validation"
)
for dir in "${RESIDUAL_DIRS[@]}"; do
    if [[ -d "$dir" ]]; then
        echo "  清除残留: $dir"
        rm -rf "$dir"
    fi
done

# 1c. 清除旧环境配置
ENV_DIR="$HOME/.config/environment.d"
ENV_FILE="$ENV_DIR/nekomote.conf"
if [[ -f "$ENV_FILE" ]]; then
    echo "  清除环境配置: $ENV_FILE"
    rm -f "$ENV_FILE"
fi

# 1d. 清除 QML 编译缓存（.qmlc 文件会让 plasmashell 继续用旧 QML 字节码）
QML_CACHE_DIRS=(
    "$HOME/.cache/plasmashell/qmlcache"
    "$HOME/.cache/kwin/qmlcache"
    "$HOME/.cache/ksplash/qmlcache"
)
for cache_dir in "${QML_CACHE_DIRS[@]}"; do
    if [[ -d "$cache_dir" ]]; then
        echo "  清除 QML 缓存: $cache_dir"
        rm -f "$cache_dir"/*.qmlc
    fi
done

# 1e. 刷新 KDE 包缓存（让 Plasma 忘掉旧 KPackage）
if command -v kbuildsycoca6 &>/dev/null; then
    echo "  刷新 KDE 包缓存 (kbuildsycoca6)..."
    kbuildsycoca6 --noincremental 2>/dev/null || true
fi

echo "  清除完成。"
echo ""

# ============================================================
# 第二步：安装新文件
# ============================================================
echo "--- 2/3 安装新文件 ---"

if [[ ! -d "$STAGE_DIR" ]]; then
    echo "错误: stage 目录不存在: $STAGE_DIR"
    echo "请先执行:"
    echo "  cmake --preset dev -DNEKOMOTE_BUILD_PLASMA=ON -DNEKOMOTE_BUILD_QT_QUICK=ON"
    echo "  cmake --build build/dev --target nekomote_plasma_qmlplugin nekomote_plasma_qml nekomote_plasma_stage"
    exit 1
fi

if [[ ! -f "$STAGE_DIR/manifest.json" ]]; then
    echo "错误: stage manifest 不存在: $STAGE_DIR/manifest.json"
    echo "请重新构建 stage 目标:"
    echo "  cmake --build build/dev --target nekomote_plasma_stage"
    exit 1
fi

python3 "$SCRIPT_DIR/plasma_deploy.py" --install --stage "$STAGE_DIR" --prefix "$PREFIX_DIR"

# 2b. 写 KDE 环境配置（QML_IMPORT_PATH）
QML_PATH="$PREFIX_DIR/nekomote/qml"
mkdir -p "$ENV_DIR"
echo "QML_IMPORT_PATH=\"$QML_PATH:\${QML_IMPORT_PATH:-}\"" > "$ENV_FILE"
echo "  环境配置: $ENV_FILE"
echo "  QML_IMPORT_PATH=$QML_PATH"

# 2c. 再次刷新 KDE 包缓存（让 Plasma 发现新 KPackage）
if command -v kbuildsycoca6 &>/dev/null; then
    echo "  刷新 KDE 包缓存 (kbuildsycoca6)..."
    kbuildsycoca6 --noincremental 2>/dev/null || true
fi

echo ""

# ============================================================
# 第三步：验证安装
# ============================================================
echo "--- 3/3 验证安装 ---"
python3 "$SCRIPT_DIR/plasma_deploy.py" --status --prefix "$PREFIX_DIR"
python3 "$SCRIPT_DIR/plasma_deploy.py" --verify --prefix "$PREFIX_DIR"
echo ""

# ============================================================
# 提示
# ============================================================
echo "=== 重装完成 ==="
echo ""
echo "下一步:"
echo "  1. 重新登录 Plasma 会话（或运行 systemctl --user import-environment QML_IMPORT_PATH）"
echo "  2. 重启 plasmashell 以加载新 QML: kquitapp6 plasmashell && kstart6 plasmashell"
echo "  3. KDE 系统设置 → 外观 → 壁纸 → 选择 \"NekoMote 壁纸\""
if [[ -n "$ASSETS_DIR" ]]; then
    echo "  4. 壁纸配置中设置 Assets 目录: file://$ASSETS_DIR"
fi
echo "  5. 壁纸配置中可勾选\"静音\"复选框控制声音"
echo ""
echo "卸载: python3 $SCRIPT_DIR/plasma_deploy.py --uninstall --prefix \"$PREFIX_DIR\""
echo "      rm -f \"$ENV_FILE\""
