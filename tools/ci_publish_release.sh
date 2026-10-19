#!/bin/bash
# Called only after all baseline jobs pass. No build or asset discovery here.
set -euo pipefail
TAG="${1:?version tag required}"
PACKAGES_DIR="${2:?package directory required}"
if [[ ! "$TAG" =~ ^v(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$ ]]; then
    echo 'Expected a stable version tag such as v0.5.0' >&2
    exit 1
fi

cd "$PACKAGES_DIR"
PACKAGES=(nekomote-native-ubuntu20-x86_64 nekomote-native-ubuntu20-aarch64 nekomote-plasma5-x86_64
    nekomote-plasma6-x86_64 nekomote-plasma6-aarch64
    nekomote-layer-shell-debian12-x86_64 nekomote-layer-shell-debian12-aarch64
    nekomote-x11-ubuntu20-x86_64 nekomote-x11-ubuntu20-aarch64)
FILES=(*)
if [[ "${#FILES[@]}" != 18 ]]; then
    echo 'Expected exactly nine packages and nine SHA256 files' >&2
    exit 1
fi
ASSETS=()
for package in "${PACKAGES[@]}"; do
    archive="$package.tar.gz"
    checksum="$archive.sha256"
    test -f "$archive" && test -s "$archive"
    test -f "$checksum" && test -s "$checksum"
    # Bind each checksum to its expected basename before sha256sum reads it.
    read -r digest recorded_name extra < "$checksum"
    if [[ ! "$digest" =~ ^[0-9a-f]{64}$ || "$recorded_name" != "$archive" || -n "$extra" ]]; then
        echo "Invalid checksum record: $checksum" >&2
        exit 1
    fi
    sha256sum --check --strict "$checksum"
    ASSETS+=("$archive" "$checksum")
done

: "${GH_REPO:?GitHub repository required}"
: "${GH_TOKEN:?GitHub token required}"
# Retry an interrupted draft upload, but never replace a published version.
if draft="$(gh release view "$TAG" --json isDraft --jq '.isDraft')"; then
    if [[ "$draft" != true ]]; then
        echo "Release $TAG is already published; use a new version tag" >&2
        exit 1
    fi
else
    gh release create "$TAG" --verify-tag --draft --title "NekoMote $TAG" --generate-notes
fi
gh release upload "$TAG" "${ASSETS[@]}" --clobber
# An upload failure leaves a draft, never a publicly incomplete release.
gh release edit "$TAG" --draft=false --prerelease=false --latest
