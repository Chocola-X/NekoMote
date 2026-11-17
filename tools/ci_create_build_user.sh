#!/bin/sh
# Image-build step only: --user UID:GID does not create NSS account records.
set -eu
build_uid="${1:?build UID required}"
build_gid="${2:?build GID required}"
if ! getent group "$build_gid" >/dev/null; then
    groupadd --gid "$build_gid" nekomote-build
fi
if ! getent passwd "$build_uid" >/dev/null; then
    useradd --uid "$build_uid" --gid "$build_gid" --no-create-home --no-log-init \
        --home-dir /nonexistent --shell /usr/sbin/nologin nekomote-build
fi
