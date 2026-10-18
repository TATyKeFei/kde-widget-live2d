#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
    printf 'Usage: %s DEPENDENCY_DIRECTORY\n' "$0" >&2
    exit 2
fi
mkdir -p -- "$1"
deps_dir=$(cd -- "$1" && pwd)

# Keep these versions in sync with the Cubism Vulkan patches and CI paths.
# Overrides let local verification use the same archives without network access.
fetch() {
    local archive=$1 url=$2 checksum=$3
    if [[ ! -f "$archive" ]]; then
        curl --fail --location --retry 3 --connect-timeout 20 --max-time 300 \
            --proto '=https' --proto-redir '=https' "$url" --output "$archive.part"
        mv -- "$archive.part" "$archive"
    fi
    printf '%s  %s\n' "$checksum" "$archive" | sha256sum --check --strict
}

sdk_archive=${CUBISM_SDK_ARCHIVE:-"$deps_dir/CubismSdkForNative-5-r.5.zip"}
glew_archive=${GLEW_ARCHIVE:-"$deps_dir/glew-2.2.0.tgz"}
fetch "$sdk_archive" \
    'https://cubism.live2d.com/sdk-native/bin/CubismSdkForNative-5-r.5.zip' \
    7ff3a4bbc19c0a8728965aa522ab77eb11b252916453e68a8a78d3b71188bb12
fetch "$glew_archive" \
    'https://downloads.sourceforge.net/project/glew/glew/2.2.0/glew-2.2.0.tgz' \
    d4fc82893cfb00109578d0a1a2337fb8ca335b3ceccf97b97e5cc7f08e4353e1

unzip -q -o "$sdk_archive" -d "$deps_dir"
tar -xzf "$glew_archive" -C "$deps_dir"
test -f "$deps_dir/CubismSdkForNative-5-r.5/Core/lib/linux/x86_64/libLive2DCubismCore.a"
test -f "$deps_dir/CubismSdkForNative-5-r.5/Framework/src/Rendering/Vulkan/CubismRenderer_Vulkan.cpp"
test -f "$deps_dir/glew-2.2.0/src/glew.c"
