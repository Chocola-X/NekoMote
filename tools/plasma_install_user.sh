#!/bin/bash
# NekoMote Plasma 壁纸插件用户安装脚本
# 用法: ./plasma_install_user.sh [--assets-dir DIR] [--stage DIR] [--prefix DIR]
# 默认 stage:  build/plasma/adapters/plasma/stage
# 默认 prefix: $XDG_DATA_HOME (或 ~/.local/share)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

STAGE_DIR="${REPO_DIR}/build/plasma/adapters/plasma/stage"
PREFIX_DIR=""
ASSETS_DIR=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --stage)     STAGE_DIR="$2"; shift 2 ;;
        --prefix)    PREFIX_DIR="$2"; shift 2 ;;
        --assets-dir) ASSETS_DIR="$2"; shift 2 ;;
        --help|-h)
            echo "用法: $0 [--assets-dir DIR] [--stage DIR] [--prefix DIR]"
            echo ""
            echo "  --assets-dir  assets 目录路径 (默认: 自动检测)"
            echo "  --stage       stage 目录 (默认: build/plasma/adapters/plasma/stage)"
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

echo "=== NekoMote Plasma 壁纸插件安装 ==="
echo "  stage:  $STAGE_DIR"
echo "  prefix: $PREFIX_DIR"
echo "  assets: ${ASSETS_DIR:-未指定}"
echo ""

if [[ ! -d "$STAGE_DIR" ]]; then
    echo "错误: stage 目录不存在: $STAGE_DIR"
    echo "请先在构建机器上运行:"
    echo "  cmake --preset plasma && cmake --build --preset plasma"
    echo "  cmake --build build/plasma --target nekomote_plasma_stage"
    exit 1
fi

python3 "$SCRIPT_DIR/plasma_deploy.py" --install --stage "$STAGE_DIR" --prefix "$PREFIX_DIR"

QML_PATH="$PREFIX_DIR/nekomote/qml"

ENV_DIR="$HOME/.config/environment.d"
mkdir -p "$ENV_DIR"
ENV_FILE="$ENV_DIR/nekomote.conf"
echo "QML_IMPORT_PATH=\"$QML_PATH:\${QML_IMPORT_PATH:-}\"" > "$ENV_FILE"
echo ""
echo "已写入环境配置: $ENV_FILE"
echo "  QML_IMPORT_PATH=$QML_PATH"
echo ""

if [[ -n "$ASSETS_DIR" ]]; then
    ASSETS_URL="file://$ASSETS_DIR"
    echo "assets 目录: $ASSETS_DIR"
    echo "在壁纸配置中将 Assets 目录设为: $ASSETS_URL"
    echo ""
fi

echo "=== 安装完成 ==="
echo ""
echo "下一步:"
echo "  1. 重新登录 Plasma 会话 (或运行 systemctl --user import-environment QML_IMPORT_PATH)"
echo "  2. 右键桌面 → 配置桌面和壁纸 → 壁纸 → 选择 \"NekoMote 壁纸\""
echo "  3. 在壁纸配置中设置 Assets 目录为: file://$ASSETS_DIR"
echo "  4. 同时选择一个模型文件和会话文件 (或通过控制菜单切换角色)"
echo "  5. 应用后，壁纸右下角会出现 ☰ 按钮，点击可打开角色切换菜单"
echo ""
echo "卸载: python3 $SCRIPT_DIR/plasma_deploy.py --uninstall --prefix $PREFIX_DIR"
echo "      rm $ENV_FILE"
