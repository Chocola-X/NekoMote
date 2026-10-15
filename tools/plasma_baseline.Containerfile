# Public Qt baseline; do not include Qt/KF/GPU libraries in the plugin package.
# Manifest digest verified against Docker Hub's historical tag, 2026-10-12.
# The image predates the apt snapshot, so matching libc development packages can
# be installed without downgrading a newer image's base libraries.
FROM debian:bookworm-20250811-slim@sha256:b1a741487078b369e78119849663d7f1a5341ef2768798f7b7406c4240f86aef

# Pin apt contents as well as the base image. Public Qt 6.4.2, GCC 12, glibc 2.36.
RUN rm -f /etc/apt/sources.list.d/debian.sources && \
    printf '%s\n' \
      'deb [check-valid-until=no] http://snapshot.debian.org/archive/debian/20250901T000000Z bookworm main' \
      > /etc/apt/sources.list && \
    apt-get update && apt-get install -y --no-install-recommends \
      ca-certificates git cmake make ninja-build g++ python3 binutils \
      qt6-base-dev qt6-declarative-dev libpulse-dev libfontconfig1-dev libpng-dev \
      libvulkan-dev glslang-tools python3-jinja2 python3-pil nodejs fonts-dejavu-core \
      qml6-module-qtquick qml6-module-qtqml-workerscript \
      qml6-module-qtquick-window && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /work
# Source mounted read-only at /source; artifacts written to explicit /output.
ENTRYPOINT ["bash", "/source/tools/ci_build.sh"]
