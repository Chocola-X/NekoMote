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

# Validate the new package before touching the existing installation.
python3 - "$STAGE_DIR" "$SCRIPT_DIR" <<'PYVERIFY'
import sys
from pathlib import Path
sys.path.insert(0, sys.argv[2])
from plasma_stage_verify import check_files, check_manifest, check_rpath
stage = Path(sys.argv[1])
for check in (check_files, check_manifest, check_rpath):
    if check(stage):
        raise SystemExit(1)
PYVERIFY

# Remove recorded plugin files only. nekomote/config.json belongs to the
# wallpaper user and must survive reinstall (especially with read-only assets).
python3 "$SCRIPT_DIR/plasma_deploy.py" --uninstall --prefix "$PREFIX_DIR"
# This old package file predates install records and is no longer used.
rm -f "$PREFIX_DIR/plasma/wallpapers/org.nekomote.wallpaper/contents/config/main.qml"
python3 "$SCRIPT_DIR/plasma_deploy.py" --install --stage "$STAGE_DIR" --prefix "$PREFIX_DIR"

ENV_DIR="${XDG_CONFIG_HOME:-$HOME/.config}/environment.d"
ENV_FILE="$ENV_DIR/nekomote.conf"

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
echo "  1. 重新登录测试用户的 Plasma 会话，让 QML_IMPORT_PATH 和新 native 库生效"
echo "  2. 在该测试用户的壁纸配置中选择 NekoMote（重新登录已加载新插件库）"
echo "  3. KDE 系统设置 → 外观 → 壁纸 → 选择 \"NekoMote 壁纸\""
if [[ -n "$ASSETS_DIR" ]]; then
    echo "  4. 壁纸配置中设置 Assets 目录: file://$ASSETS_DIR"
fi
echo "  5. 点击右上角图标，在共享菜单的设置中选择背景（未选择时为纯色底）"
echo ""
echo "卸载: python3 $SCRIPT_DIR/plasma_deploy.py --uninstall --prefix \"$PREFIX_DIR\""
echo "      rm -f \"$ENV_FILE\""
