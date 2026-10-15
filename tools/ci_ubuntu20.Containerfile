# The host runner is recent; all compilation and tests execute inside focal.
FROM ubuntu:20.04@sha256:8feb4d8ca5354def3d8fce243717141ce31e2c428701f6682bd2fafe15388214

ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
      ca-certificates curl git gcc-10 g++-10 make ninja-build pkg-config binutils \
      libpng-dev zlib1g-dev libpulse-dev libfontconfig1-dev libgl1-mesa-dev \
      libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev \
      libwayland-dev libxkbcommon-dev libvulkan-dev glslang-tools \
      qtbase5-dev qtdeclarative5-dev libqt5opengl5-dev \
      qml-module-qtquick2 qml-module-qtquick-window2 qml-module-qtquick-controls \
      qml-module-qtquick-controls2 \
      qml-module-qtquick-layouts qml-module-qtquick-dialogs \
      qml-module-qt-labs-folderlistmodel qml-module-qt-labs-settings \
      fonts-dejavu-core && rm -rf /var/lib/apt/lists/*

# Python 3.8 in focal cannot run existing repository tools (Python >=3.10).
# Pin a standalone build tool, not a runtime dependency of the native programs.
RUN curl -fL --retry 3 \
      'https://github.com/astral-sh/python-build-standalone/releases/download/20251014/cpython-3.11.14%2B20251014-x86_64-unknown-linux-gnu-install_only.tar.gz' \
      -o /tmp/python.tar.gz && \
    echo 'd0623c777fb89b904b56cd5aba51af29cbb34b1f9d45f0672f90f6dce30fa93e  /tmp/python.tar.gz' | sha256sum -c - && \
    tar -C /opt -xzf /tmp/python.tar.gz && rm /tmp/python.tar.gz
ENV PATH="/opt/python/bin:${PATH}" CC=gcc-10 CXX=g++-10
RUN python3 -m pip install --no-cache-dir cmake==3.28.4 Jinja2==3.1.6 MarkupSafe==3.0.3 Pillow==11.1.0

# Offline session-import tests execute project-authored JavaScript. Focal's
# Node 10 does not support the node: built-in imports used by those tools.
RUN curl -fL --retry 3 https://nodejs.org/dist/v22.14.0/node-v22.14.0-linux-x64.tar.gz \
      -o /tmp/node.tar.gz && \
    echo '9d942932535988091034dc94cc5f42b6dc8784d6366df3a36c4c9ccb3996f0c2  /tmp/node.tar.gz' | sha256sum -c - && \
    mkdir -p /opt/node && tar -C /opt/node --strip-components=1 -xzf /tmp/node.tar.gz && \
    rm /tmp/node.tar.gz
ENV PATH="/opt/node/bin:${PATH}"

WORKDIR /work
ENTRYPOINT ["bash", "/source/tools/ci_build.sh"]
