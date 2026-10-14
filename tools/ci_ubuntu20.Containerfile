# The host runner is recent; all compilation and tests execute inside focal.
FROM ubuntu:20.04@sha256:8feb4d8ca5354def3d8fce243717141ce31e2c428701f6682bd2fafe15388214

ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
      ca-certificates curl git gcc-10 g++-10 ninja-build pkg-config binutils \
      libpng-dev zlib1g-dev libpulse-dev libfontconfig1-dev libgl1-mesa-dev \
      libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev \
      libwayland-dev libxkbcommon-dev libvulkan-dev glslang-tools \
      qtbase5-dev qtdeclarative5-dev libqt5opengl5-dev \
      qml-module-qtquick2 qml-module-qtquick-window2 qml-module-qtquick-controls2 \
      qml-module-qtquick-layouts qml-module-qtquick-dialogs \
      fonts-dejavu-core && rm -rf /var/lib/apt/lists/*

# Python 3.8 in focal cannot run existing repository tools (Python >=3.10).
# Pin a standalone build tool, not a runtime dependency of the native programs.
RUN curl -fL --retry 3 \
      'https://github.com/astral-sh/python-build-standalone/releases/download/20251014/cpython-3.11.14%2B20251014-x86_64-unknown-linux-gnu-install_only.tar.gz' \
      -o /tmp/python.tar.gz && \
    echo 'd0623c777fb89b904b56cd5aba51af29cbb34b1f9d45f0672f90f6dce30fa93e  /tmp/python.tar.gz' | sha256sum -c - && \
    tar -C /opt -xzf /tmp/python.tar.gz && rm /tmp/python.tar.gz
ENV PATH="/opt/python/bin:${PATH}" CC=gcc-10 CXX=g++-10
RUN python3 -m pip install --no-cache-dir cmake==3.28.4 Jinja2==3.1.6 MarkupSafe==3.0.3

WORKDIR /work
ENTRYPOINT ["bash", "/source/tools/ci_build.sh"]
