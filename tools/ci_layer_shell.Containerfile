# Standalone Wayland/EGL/Vulkan shell baseline; no Qt or bundled system libraries.
# Manifest digest verified against Docker Hub's historical tag, 2026-10-12.
# The image predates the apt snapshot, so matching libc development packages can
# be installed without downgrading a newer image's base libraries.
FROM debian:bookworm-20250811-slim@sha256:b1a741487078b369e78119849663d7f1a5341ef2768798f7b7406c4240f86aef

# Pin apt contents as well as the base image. Wayland 1.21, GCC 12, glibc 2.36.
RUN rm -f /etc/apt/sources.list.d/debian.sources && \
    printf '%s\n' \
      'deb [check-valid-until=no] http://snapshot.debian.org/archive/debian/20250901T000000Z bookworm main' \
      > /etc/apt/sources.list && \
    apt-get update && apt-get install -y --no-install-recommends \
      ca-certificates git cmake make ninja-build g++ python3 binutils \
      libvulkan-dev glslang-tools \
      pkg-config libwayland-dev libegl-dev libgl-dev libpulse-dev libfontconfig1-dev libpng-dev \
      python3-jinja2 python3-pil nodejs fonts-dejavu-core && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /work
# Source mounted read-only at /source; artifacts written to explicit /output.
ENTRYPOINT ["bash", "/source/tools/ci_build.sh"]
