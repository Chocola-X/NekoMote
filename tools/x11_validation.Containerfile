# Build tools/ci_ubuntu20.Containerfile as nekomote-ci20 first.
# Optional desktop test image; never a dependency of release builds.
FROM nekomote-ci20
RUN apt-get update && apt-get install -y --no-install-recommends \
    xserver-xephyr xfwm4 xfdesktop4 xfconf dbus-x11 x11-utils x11-xserver-utils \
    mesa-utils libgl1-mesa-dri libxext-dev && rm -rf /var/lib/apt/lists/*
ENTRYPOINT ["bash"]
