# Shared release toolchain for the native programs and the X11 shell.
FROM almalinux:8.10@sha256:768bedc30fe0a536adb0a65d2eddb7caad364e514fbddd0ae9e026975f590342

RUN dnf -y --setopt=install_weak_deps=False --enablerepo=powertools install \
      ca-certificates curl git make cmake ninja-build pkgconf-pkg-config binutils \
      gcc-toolset-13-gcc gcc-toolset-13-gcc-c++ gcc-toolset-13-binutils \
      python3.11 python3.11-pip tar gzip shadow-utils \
      libpng-devel zlib-devel dbus dbus-tools dbus-devel pulseaudio-libs-devel \
      fontconfig-devel dejavu-sans-fonts libglvnd-devel libX11-devel libXrandr-devel \
      libXext-devel libXinerama-devel libXcursor-devel libXi-devel \
      wayland-devel libxkbcommon-devel vulkan-loader-devel && \
    test -x /opt/rh/gcc-toolset-13/root/usr/bin/ld && dnf clean all

# Use Toolset's compiler and linker, but keep system binutils on PATH for the
# audit. No Toolset runtime library search path is needed for the programs.
ENV CC=/opt/rh/gcc-toolset-13/root/usr/bin/gcc \
    CXX=/opt/rh/gcc-toolset-13/root/usr/bin/g++ \
    COMPILER_PATH=/opt/rh/gcc-toolset-13/root/usr/bin \
    PATH="/opt/python/bin:/opt/node/bin:/opt/glslang/bin:${PATH}"
RUN python3.11 -m venv /opt/python && \
    python3 -m pip install --no-cache-dir Jinja2==3.1.6 MarkupSafe==3.0.3 Pillow==11.1.0

# Compile the pinned build-time shader tool with the same baseline toolchain.
RUN curl -fL --retry 3 \
      https://codeload.github.com/KhronosGroup/glslang/tar.gz/refs/tags/12.0.0 \
      -o /tmp/glslang.tar.gz && \
    echo '7cb45842ec1d4b6ea775d624c3d2d8ba9450aa416b0482b0cc7e4fdd399c3d75  /tmp/glslang.tar.gz' \
      | sha256sum -c - && \
    tar --no-same-owner -C /tmp -xzf /tmp/glslang.tar.gz && \
    cmake -S /tmp/glslang-12.0.0 -B /tmp/glslang-build -G Ninja \
      -DCMAKE_BUILD_TYPE=Release -DENABLE_OPT=OFF -DBUILD_EXTERNAL=OFF \
      -DENABLE_CTEST=OFF -DENABLE_SPVREMAPPER=OFF && \
    cmake --build /tmp/glslang-build --target glslangValidator -j 4 && \
    install -D /tmp/glslang-build/StandAlone/glslangValidator /opt/glslang/bin/glslangValidator && \
    rm -rf /tmp/glslang.tar.gz /tmp/glslang-12.0.0 /tmp/glslang-build

RUN case "$(uname -m)" in \
      x86_64) node_arch=x64; node_sha=9d942932535988091034dc94cc5f42b6dc8784d6366df3a36c4c9ccb3996f0c2 ;; \
      aarch64) node_arch=arm64; node_sha=8cf30ff7250f9463b53c18f89c6c606dfda70378215b2c905d0a9a8b08bd45e0 ;; \
      *) echo 'Unsupported AlmaLinux 8 build architecture' >&2; exit 1 ;; \
    esac && curl -fL --retry 3 "https://nodejs.org/dist/v22.14.0/node-v22.14.0-linux-${node_arch}.tar.gz" \
      -o /tmp/node.tar.gz && \
    echo "${node_sha}  /tmp/node.tar.gz" | sha256sum -c - && \
    mkdir -p /opt/node && tar --no-same-owner -C /opt/node --strip-components=1 -xzf /tmp/node.tar.gz && \
    rm /tmp/node.tar.gz

ARG CI_UID=1000
ARG CI_GID=1000
COPY ci_create_build_user.sh /usr/local/libexec/nekomote-ci-create-build-user.sh
RUN sh /usr/local/libexec/nekomote-ci-create-build-user.sh "$CI_UID" "$CI_GID"

WORKDIR /work
ENTRYPOINT ["bash", "/source/tools/ci_build.sh"]
