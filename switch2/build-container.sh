#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu
cd "$(dirname "$0")"
sdk=ounce-devkit-20261117.tar.xz
sdk_sha=33cc5478324f8243dc801c3d4b8fe781885a306975f63937bd246ffdfd73cf79
image=devkitpro/devkita64:20260219@sha256:1fc388c3a0d34bd2045a6dadcb1020e069d5f876a187fd705de14b4440c00282
staging=$(mktemp -d)
trap 'rm -rf "$staging"' EXIT
curl -fsSL --retry 3 -o "$staging/$sdk" \
    "https://github.com/ouncebrew/ounce-devkit/releases/download/devkit-20261117/$sdk"
(cd "$staging" && printf '%s  %s\n' "$sdk_sha" "$sdk" | sha256sum -c -)

docker run --rm --platform linux/amd64 --user "$(id -u):$(id -g)" \
    -v "$PWD:/project" -v "$staging/$sdk:/sdk.tar.xz:ro" -w /project "$image" \
    sh -eu -c '
        sdk_root=/tmp/ounce-sdk
        mkdir "$sdk_root"
        tar xf /sdk.tar.xz -C "$sdk_root"
        ln -s /opt/devkitpro/devkitA64 /opt/devkitpro/tools "$sdk_root/"
        export DEVKITPRO="$sdk_root" DEVKITA64="$sdk_root/devkitA64"
        exec make BUILD=build/container "$@"
    ' sh "$@"
