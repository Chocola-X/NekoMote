#!/bin/bash
# Build/test/package inside an explicit baseline container. Never install a desktop.
set -euo pipefail
MODE="${1:?native20, plasma5 or plasma6 required}"
SOURCE_DIR="${2:-/source}"
OUTPUT_DIR="${3:-/output}"
JOBS="${NEKOMOTE_BUILD_JOBS:-4}"
ARCH_NAME="$(uname -m)"
case "$MODE" in native20|plasma5|plasma6) ;; *) echo "Unknown mode: $MODE" >&2; exit 2 ;; esac
test -f "$SOURCE_DIR/CMakeLists.txt"
test -d "$OUTPUT_DIR"
WORK_DIR="$(mktemp -d /tmp/nekomote-ci.XXXXXX)"
trap 'rm -rf "$WORK_DIR"' EXIT
mkdir -p "$WORK_DIR/src/build" "$WORK_DIR/runtime"
chmod 700 "$WORK_DIR/runtime"
export XDG_RUNTIME_DIR="$WORK_DIR/runtime"
tar -C "$SOURCE_DIR" --exclude='./.git' --exclude='./build' --exclude='./assets' \
    --exclude='./refer' --exclude='./experiments' --exclude='./node_modules' \
    -cf - . | tar -C "$WORK_DIR/src" -xf -
dpkg-query -W -f='${Package}\t${Version}\n' > "$OUTPUT_DIR/toolchain-packages.txt"
# GNU LTO invokes make even though Ninja drives the outer build. Check the
# complete CPU/tool test environment before starting an expensive compilation.
{
    cmake --version
    "${CC:-cc}" --version
    "${CXX:-c++}" --version
    make --version
    python3 --version
    node --version
    python3 - <<'PY'
import jinja2
from PIL import Image
print("Jinja2", jinja2.__version__, "Pillow", Image.__version__)
print("Resampling", Image.Resampling.NEAREST)
PY
    node -e "require('node:vm'); require('node:assert/strict'); require('node:fs')"
} > "$OUTPUT_DIR/toolchain.txt"

OPTIONS=(-DCMAKE_BUILD_TYPE=Release -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON
    -DNEKOMOTE_WERROR=ON -DNEKOMOTE_BUILD_TOOLS=OFF -DNEKOMOTE_BUILD_TESTS=ON
    -DNEKOMOTE_BUILD_ABI=ON -DNEKOMOTE_BUILD_VULKAN=ON)
TARGETS=(nekomote_unit_tests nekomote_audio_device_tests nekomote_font_probe
    nekomote_abi_negative_test nekomote_abi_boundary_tests)
if [[ "$MODE" == native20 ]]; then
    OPTIONS+=(-DNEKOMOTE_BUILD_PREVIEW=ON -DNEKOMOTE_BUILD_PLAYER=ON
        -DNEKOMOTE_BUILD_QT_QUICK=OFF -DNEKOMOTE_BUILD_PLASMA=OFF)
    TARGETS+=(nekomote_player nekomote_import nekomote_abi_host)
else
    QT_MAJOR=6
    [[ "$MODE" != plasma5 ]] || QT_MAJOR=5
    OPTIONS+=(-DNEKOMOTE_BUILD_PREVIEW=OFF -DNEKOMOTE_BUILD_PLAYER=OFF
        -DNEKOMOTE_BUILD_QT_QUICK=ON -DNEKOMOTE_BUILD_PLASMA=ON
        -DNEKOMOTE_BUILD_PLASMA_PROBE=OFF "-DNEKOMOTE_QT_MAJOR=$QT_MAJOR")
    TARGETS+=(nekomote_plasma_module_test)
    [[ "$MODE" != plasma5 ]] || TARGETS+=(nekomote_plasma5_package_test)
fi
cmake -S "$WORK_DIR/src" -B "$WORK_DIR/build" -G Ninja "${OPTIONS[@]}"
cmake --build "$WORK_DIR/build" --target "${TARGETS[@]}" -j "$JOBS"

# In-process Qt plugins must use the host C++ runtime, not accidentally fold an
# extra static libstdc++ into plasmashell. Verify the real ELF, not just flags.
if [[ "$MODE" != native20 ]]; then
    readelf -d "$WORK_DIR/build/lib/libnekomote_plasma_qml.so" \
        > "$OUTPUT_DIR/plugin-dynamic-dependencies.txt"
    grep -Fq 'Shared library: [libstdc++.so.6]' "$OUTPUT_DIR/plugin-dynamic-dependencies.txt"
fi

# Pure synthetic CPU/ABI/Python tests are mandatory. Assets and a desktop are
# absent by design; integration/GPU tests are excluded and reported separately.
ctest --test-dir "$WORK_DIR/build" -LE 'gpu|integration' --no-tests=error \
    --output-on-failure --parallel "$JOBS" --output-junit "$OUTPUT_DIR/tests.xml"
ctest --test-dir "$WORK_DIR/build" -N -L 'gpu|integration' \
    > "$OUTPUT_DIR/excluded-tests.txt"
mkdir "$WORK_DIR/package"
if [[ "$MODE" == native20 ]]; then
    STAGE_DIR="$WORK_DIR/package/nekomote-native"
    cmake --install "$WORK_DIR/build" --prefix "$STAGE_DIR" --component runtime
    NAME="nekomote-native-ubuntu20-$ARCH_NAME"
else
    STAGE_DIR="$WORK_DIR/build/adapters/plasma/stage"
    if [[ "$MODE" == plasma5 ]]; then
        STAGE_DIR="$WORK_DIR/build/adapters/plasma5/stage"
    fi
    NAME="nekomote-$MODE-$ARCH_NAME"
fi

mkdir -p "$STAGE_DIR/licenses"
for dependency in nlohmann_json stb glad; do
    LICENSE_NAME=LICENSE
    [[ "$dependency" != nlohmann_json ]] || LICENSE_NAME=LICENSE.MIT
    cp "$WORK_DIR/build/_deps/$dependency-src/$LICENSE_NAME" "$STAGE_DIR/licenses/$dependency.txt"
done
if [[ "$MODE" == native20 ]]; then
    cp "$WORK_DIR/build/_deps/glfw-src/LICENSE.md" "$STAGE_DIR/licenses/glfw.txt"
fi
cp "$WORK_DIR/src/LICENSE" "$STAGE_DIR/licenses/NekoMote.txt"
cp "$WORK_DIR/build/_deps/fast_float-src/LICENSE-MIT" "$STAGE_DIR/licenses/fast_float-MIT.txt"
cp "$WORK_DIR/src/tools/licenses/"*.txt "$STAGE_DIR/licenses/"
if [[ "$MODE" == native20 ]]; then
    python3 "$WORK_DIR/src/tools/ci_native_audit.py" "$STAGE_DIR" \
        > "$OUTPUT_DIR/compatibility.json"
    cp "$OUTPUT_DIR/compatibility.json" "$STAGE_DIR/compatibility.json"
    cp "$WORK_DIR/src/docs/github-actions.md" "$STAGE_DIR/README.md"
    tar -C "$WORK_DIR/package" -czf "$OUTPUT_DIR/$NAME.tar.gz" nekomote-native
else
    python3 "$WORK_DIR/src/tools/plasma_stage_manifest.py" "$STAGE_DIR"
    if [[ "$MODE" == plasma5 ]]; then
        QT_VERSION="$(qmake -query QT_VERSION)"
        BASELINE=--baseline-ubuntu20
    else
        QT_VERSION="$(qmake6 -query QT_VERSION)"
        BASELINE=--baseline-debian12
    fi
    python3 "$WORK_DIR/src/tools/plasma_distribution.py" package "$STAGE_DIR" \
        --qt-version "$QT_VERSION" "$BASELINE" --output "$OUTPUT_DIR/$NAME.tar.gz" \
        > "$OUTPUT_DIR/compatibility.json"
fi
(cd "$OUTPUT_DIR" && sha256sum "$NAME.tar.gz" > "$NAME.tar.gz.sha256")
echo "Package: $NAME.tar.gz"
