FROM ubuntu:22.04@sha256:f64b2ffc2f10ab8f8505aa56b2664bd846cbb9df87c7e2532e30f7667021c0e6

ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
      ca-certificates curl git gcc-12 g++-12 make ninja-build pkg-config binutils \
      python3 python3-pip libglib2.0-bin libwayland-dev libegl-dev libgl-dev \
      libvulkan-dev glslang-tools dbus libdbus-1-dev libpulse-dev libfontconfig1-dev \
      libpng-dev zlib1g-dev fonts-dejavu-core && rm -rf /var/lib/apt/lists/*
ENV CC=gcc-12 CXX=g++-12
RUN python3 -m pip install --no-cache-dir cmake==3.28.4 Jinja2==3.1.6 MarkupSafe==3.0.3 Pillow==11.1.0

# Use the same Node version as the other Ubuntu build for the import tests.
RUN case "$(dpkg --print-architecture)" in \
      amd64) node_arch=x64; node_sha=9d942932535988091034dc94cc5f42b6dc8784d6366df3a36c4c9ccb3996f0c2 ;; \
      arm64) node_arch=arm64; node_sha=8cf30ff7250f9463b53c18f89c6c606dfda70378215b2c905d0a9a8b08bd45e0 ;; \
      *) echo 'Unsupported Ubuntu22 build architecture' >&2; exit 1 ;; \
    esac && curl -fL --retry 3 "https://nodejs.org/dist/v22.14.0/node-v22.14.0-linux-${node_arch}.tar.gz" \
      -o /tmp/node.tar.gz && \
    echo "${node_sha}  /tmp/node.tar.gz" | sha256sum -c - && \
    mkdir -p /opt/node && tar -C /opt/node --strip-components=1 -xzf /tmp/node.tar.gz && \
    rm /tmp/node.tar.gz
ENV PATH="/opt/node/bin:${PATH}"

# Match the unprivileged runner used by docker run --user (D-Bus needs NSS).
ARG CI_UID=1000
ARG CI_GID=1000
COPY ci_create_build_user.sh /usr/local/libexec/nekomote-ci-create-build-user.sh
RUN sh /usr/local/libexec/nekomote-ci-create-build-user.sh "$CI_UID" "$CI_GID"

WORKDIR /work
ENTRYPOINT ["bash", "/source/tools/ci_build.sh"]
