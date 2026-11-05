#!/bin/bash
set -euo pipefail

project=$(cd "$(dirname "$0")" && pwd)
scratch="${1:-$project/build/native}"
architecture=$(uname -m)
source=$(cd "$project/../.." && pwd)
mkdir -p "$scratch"
scratch=$(cd "$scratch" && pwd)
work="$scratch/$architecture"
mkdir -p "$work"

export MACOSX_DEPLOYMENT_TARGET=13.0
python3 "$source/scripts/build_dependencies.py" --scratch "$work/dependencies"
deps="$work/dependencies/deps"

# Apply the menu bar telemetry to a copy of the service source for the Mac build.
mac_source="$work/macos-source"
stage=$(mktemp -d "$scratch/stage.XXXXXX")
trap 'rm -rf "$stage"' EXIT
mkdir -p "$mac_source" "$stage/source"
cp -R "$source/CMakeLists.txt" "$source/VERSION" "$source/dependencies" "$source/src" "$stage/source/"
patch -s -F 0 -d "$stage/source" -p1 < "$project/Decoder/session-activity.patch"
rsync -rc --delete "$stage/source/" "$mac_source/"

cmake -S "$project/Decoder" -B "$work/cmake" \
    -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
    -DCMAKE_OSX_ARCHITECTURES="$architecture" -DCMAKE_OSX_DEPLOYMENT_TARGET="$MACOSX_DEPLOYMENT_TARGET" \
    -DCMAKE_SKIP_RPATH=ON \
    -DCSAD_SOURCE="$mac_source" -DCSAD_DEPS="$deps"
cmake --build "$work/cmake" --parallel 4

mkdir "$stage/product"
stage_product="$stage/product"
decoder=cinemaseat-audio-decoder
cp "$work/cmake/decoder/$decoder" "$stage_product/"
for library in avcodec avutil; do
    cp -L "$deps/lib/lib$library.dylib" "$stage_product/lib$library.dylib"
    install_name_tool -id "@loader_path/lib$library.dylib" "$stage_product/lib$library.dylib"
done

# Rewrite the install names from FFmpeg's build prefix to the bundle location.
for file in "$stage_product/$decoder" "$stage_product/libavcodec.dylib" "$stage_product/libavutil.dylib"; do
    while IFS= read -r dependency; do
        case "$dependency" in
            *libavcodec*.dylib) library=avcodec ;;
            *libavutil*.dylib) library=avutil ;;
            *) continue ;;
        esac
        replacement="@loader_path/lib$library.dylib"
        if [[ "$dependency" != "$replacement" ]]; then
            install_name_tool -change "$dependency" "$replacement" "$file"
        fi
    done < <(otool -L "$file" | sed -n '2,$s/^[[:space:]]*\(.*\) (compatibility version.*$/\1/p')
    codesign --force --sign - "$file"
done

python3 "$source/scripts/check_codec_registry.py" "$stage_product/$decoder"
cp "$source/THIRD_PARTY_NOTICES.md" "$stage_product/"
cp "$source/dependencies/manifest.json" "$stage_product/dependencies.json"
cp "$work/dependencies"/ffmpeg-source/*/COPYING.LGPLv2.1 "$stage_product/FFmpeg-LGPL-2.1.txt"
cp "$work/dependencies"/boost-source/*/LICENSE_1_0.txt "$stage_product/Boost-1.0.txt"
mkdir -p "$scratch/product-$architecture"
ditto "$stage_product" "$scratch/product-$architecture"
echo "Decoder prepared at $scratch/product-$architecture"
