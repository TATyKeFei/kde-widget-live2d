#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 4 ]]; then
    printf 'Usage: %s BUILD_DIRECTORY OUTPUT_DIRECTORY CUBISM_SDK_ROOT GLEW_SOURCE_ROOT\n' "$0" >&2
    exit 2
fi
repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
build_dir=$(cd -- "$1" && pwd)
mkdir -p -- "$2"
output_dir=$(cd -- "$2" && pwd)
sdk_dir=$(cd -- "$3" && pwd)
glew_dir=$(cd -- "$4" && pwd)
commit=$(git -C "$repo_dir" rev-parse --short=12 HEAD)
bundle_name="kde-widget-live2d-linux-$(uname -m)-$commit"
work_dir=$(mktemp -d)
trap 'rm -rf -- "$work_dir"' EXIT
bundle_dir="$work_dir/$bundle_name"
mkdir -p -- "$bundle_dir/payload" "$bundle_dir/licenses"

# Use a relative lib directory at configure time, so helpers remain relocatable.
cmake --install "$build_dir" --prefix "$bundle_dir/payload" --strip
test -f "$bundle_dir/payload/lib/qt6/qml/org/kde/plasma/live2d/liblive2dbackend.so"
test -f "$bundle_dir/payload/lib/qt6/qml/org/kde/plasma/live2d/liblive2dbackendplugin.so"
test -x "$bundle_dir/payload/libexec/kde-live2d/live2d-render-opengl"
test -x "$bundle_dir/payload/libexec/kde-live2d/live2d-render-vulkan"
test -x "$bundle_dir/payload/bin/live2d-preview"

install -m 755 "$repo_dir/scripts/install-bundle.sh" "$bundle_dir/install.sh"
install -m 644 "$repo_dir/LICENSE" "$bundle_dir/licenses/PROJECT-LICENSE"
install -m 644 "$sdk_dir/LICENSE.md" "$bundle_dir/licenses/Cubism-SDK.md"
install -m 644 "$sdk_dir/Core/LICENSE.md" "$bundle_dir/licenses/Cubism-Core.md"
install -m 644 "$sdk_dir/Framework/LICENSE.md" "$bundle_dir/licenses/Cubism-Framework.md"
install -m 644 "$glew_dir/LICENSE.txt" "$bundle_dir/licenses/GLEW.txt"
install -m 644 "$repo_dir/README.md" "$bundle_dir/README.md"
# Preserve notices with the installed plugin, too.
install -d "$bundle_dir/payload/share/doc/kde-widget-live2d"
cp -a -- "$bundle_dir/licenses" "$bundle_dir/payload/share/doc/kde-widget-live2d/"

{
    printf 'Commit: %s\nArchitecture: %s\n' "$commit" "$(uname -m)"
    printf 'Cubism SDK: Native 5 R5\nGLEW: 2.2.0 (EGL, statically linked)\n'
    printf 'Renderers: Vulkan + OpenGL\nSystem libraries are not bundled.\n\n'
    cat /etc/os-release
    if command -v pacman >/dev/null 2>&1; then
        printf '\nBuild environment packages:\n'
        pacman -Q
    fi
} > "$bundle_dir/BUILD-INFO.txt"

tar -czf "$output_dir/$bundle_name.tar.gz" -C "$work_dir" "$bundle_name"
(
    cd -- "$output_dir"
    sha256sum "$bundle_name.tar.gz" > "$bundle_name.tar.gz.sha256"
)
printf 'Bundle: %s/%s.tar.gz\n' "$output_dir" "$bundle_name"
