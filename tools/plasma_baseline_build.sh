#!/bin/bash
# Intended container entry point; no installation into a running desktop.
set -euo pipefail

SOURCE_DIR="${1:?source directory required}"
OUTPUT_DIR="${2:?output directory required}"
ARCH_NAME="$(uname -m)"
if [[ ! -f "$SOURCE_DIR/CMakeLists.txt" || ! -d "$OUTPUT_DIR" ]]; then
    echo "source must contain CMakeLists.txt; output directory must already exist" >&2
    exit 1
fi
WORK_DIR="$(mktemp -d /tmp/nekomote-baseline.XXXXXX)"
trap 'rm -rf "$WORK_DIR"' EXIT
mkdir "$WORK_DIR/src"
tar -C "$SOURCE_DIR" --exclude='./.git' --exclude='./build' --exclude='./assets' \
    --exclude='./refer' -cf - . | tar -C "$WORK_DIR/src" -xf -

cmake -S "$WORK_DIR/src" -B "$WORK_DIR/build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON \
    -DNEKOMOTE_WERROR=ON -DNEKOMOTE_BUILD_PREVIEW=OFF \
    -DNEKOMOTE_BUILD_PLAYER=OFF -DNEKOMOTE_BUILD_TOOLS=OFF \
    -DNEKOMOTE_BUILD_TESTS=OFF -DNEKOMOTE_BUILD_ABI=ON \
    -DNEKOMOTE_BUILD_QT_QUICK=ON -DNEKOMOTE_BUILD_PLASMA=ON \
    -DNEKOMOTE_BUILD_PLASMA_PROBE=OFF -DNEKOMOTE_BUILD_VULKAN=ON
cmake --build "$WORK_DIR/build" --target nekomote_plasma_stage -j "${NEKOMOTE_BUILD_JOBS:-4}"

# Preserve notices of statically incorporated dependencies in the archive. Qt,
# Pulse and Vulkan remain system libraries; no binaries from those are bundled.
STAGE_DIR="$WORK_DIR/build/adapters/plasma/stage"
mkdir -p "$STAGE_DIR/licenses"
cp "$WORK_DIR/build/_deps/nlohmann_json-src/LICENSE.MIT" "$STAGE_DIR/licenses/nlohmann-json.txt"
cp "$WORK_DIR/build/_deps/stb-src/LICENSE" "$STAGE_DIR/licenses/stb.txt"
cp "$WORK_DIR/build/_deps/glad-src/LICENSE" "$STAGE_DIR/licenses/glad.txt"
cp "$WORK_DIR/src/tools/licenses/Apache-2.0.txt" "$STAGE_DIR/licenses/Apache-2.0.txt"
cp "$WORK_DIR/src/tools/licenses/CC0-1.0.txt" "$STAGE_DIR/licenses/CC0-1.0.txt"
if [[ -f "$WORK_DIR/src/LICENSE" ]]; then
    cp "$WORK_DIR/src/LICENSE" "$STAGE_DIR/licenses/NekoMote.txt"
fi
python3 "$WORK_DIR/src/tools/plasma_stage_manifest.py" "$STAGE_DIR"
dpkg-query -W -f='${Package}\t${Version}\n' > "$OUTPUT_DIR/toolchain-packages-$ARCH_NAME.txt"

QT_BUILD_VERSION="$(qmake6 -query QT_VERSION)"
ARCHIVE="$OUTPUT_DIR/nekomote-plasma-debian12-$ARCH_NAME.tar.gz"
python3 "$WORK_DIR/src/tools/plasma_distribution.py" package \
    "$WORK_DIR/build/adapters/plasma/stage" --qt-version "$QT_BUILD_VERSION" \
    --baseline-debian12 --output "$ARCHIVE" > "$OUTPUT_DIR/compatibility-$ARCH_NAME.json"
echo "Package: $ARCHIVE"
