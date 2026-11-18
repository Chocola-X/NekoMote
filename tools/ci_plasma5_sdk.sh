#!/bin/bash
# Build a Qt 5.12.8 SDK for the EL8 CI image only. The release uses the host Qt.
set -euo pipefail
prefix=/opt/qt5.12
jobs="${NEKOMOTE_QT_JOBS:-4}"
work="$(mktemp -d /tmp/nekomote-qt5.XXXXXX)"
trap 'rm -rf "$work"' EXIT
cd "$work"

# Qt 5.12 builds with the EL8 system compiler. NekoMote uses GCC Toolset 13.
unset CC CXX COMPILER_PATH LIBRARY_PATH
export PATH="$prefix/bin:/opt/python/bin:/usr/bin:/bin"
export LD_LIBRARY_PATH="$prefix/lib"
while read -r checksum archive; do
    curl -fL --retry 3 \
        "https://download.qt.io/archive/qt/5.12/5.12.8/submodules/$archive" -o "$archive"
    echo "$checksum  $archive" | sha256sum -c -
    tar --no-same-owner -xf "$archive"
done <<'CHECKSUMS'
19592fbd0a524a17c35e413988fe494251103619ef7dd49aecdf3170973aabd8 qtbase-everywhere-src-5.12.8.tar.xz
7e1059be9f3faab6fd3d40b58666798963bb9d9c5442d5d16e6870c51327160b qtdeclarative-everywhere-src-5.12.8.tar.xz
5d93b4107d4b14115a4d1692b533790b43d15cdf81325036ca15044592c81d38 qtquickcontrols-everywhere-src-5.12.8.tar.xz
a0c5646b27290cfdea6f26c58b9ddd6f177b39ac2e6d27cd7e19d8b560dc4920 qtquickcontrols2-everywhere-src-5.12.8.tar.xz
660acc87a084d2e404c2c5ddc0317f0d8a7b5292c889c9f37134e32ed7a33954 qtgraphicaleffects-everywhere-src-5.12.8.tar.xz
21347e8e2422689d63a08195e27c637983c0d4261d5205ce6b493a2adfe826b4 qttools-everywhere-src-5.12.8.tar.xz
CHECKSUMS

mkdir qtbase-build
cd qtbase-build
"$work/qtbase-everywhere-src-5.12.8/configure" -prefix "$prefix" \
    -opensource -confirm-license -release -shared -nomake examples -nomake tests \
    -opengl desktop -no-xcb -no-dbus -no-glib -no-icu -no-openssl -no-eglfs \
    -no-kms -no-gbm -no-linuxfb -no-cups
make -s -j "$jobs"
make -s install

for module in qtdeclarative qtquickcontrols qtquickcontrols2 qtgraphicaleffects; do
    mkdir "$work/$module-build"
    cd "$work/$module-build"
    qmake "$work/$module-everywhere-src-5.12.8/$module.pro"
    make -s -j "$jobs"
    make -s install
done

# Only the three Linguist CLI tools and their upstream CMake config are used.
mkdir "$work/qttools-build"
cd "$work/qttools-build"
qmake "$work/qttools-everywhere-src-5.12.8/qttools.pro"
make -s qmake_all
cd src/linguist
make -s -j "$jobs" sub-lrelease sub-lupdate sub-lconvert
make -s install_cmake_linguist_tools_files
for tool in lrelease lupdate lconvert; do
    make -s -C "$tool" install
done
test "$(qmake -query QT_VERSION)" = 5.12.8
