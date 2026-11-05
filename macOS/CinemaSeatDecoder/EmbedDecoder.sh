#!/bin/bash
set -euo pipefail

source="${CSAD_PREPARED_DIR:-$SRCROOT/build/native/product-$NATIVE_ARCH_ACTUAL}"
helpers="$TARGET_BUILD_DIR/$CONTENTS_FOLDER_PATH/Helpers"
resources="$TARGET_BUILD_DIR/$UNLOCALIZED_RESOURCES_FOLDER_PATH"
decoder=cinemaseat-audio-decoder

if [[ ! -x "$source/$decoder" ]]; then
    echo "error: Run $SRCROOT/prepare-decoder.sh before building CinemaSeat Decoder." >&2
    exit 1
fi

mkdir -p "$helpers" "$resources"
for file in "$decoder" libavcodec.dylib libavutil.dylib; do
    cp "$source/$file" "$helpers/$file"
    codesign --force --sign "${EXPANDED_CODE_SIGN_IDENTITY:--}" "$helpers/$file"
done
for file in THIRD_PARTY_NOTICES.md dependencies.json FFmpeg-LGPL-2.1.txt Boost-1.0.txt; do
    cp "$source/$file" "$resources/"
done
