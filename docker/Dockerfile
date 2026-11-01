FROM debian:bookworm-slim@sha256:99c0b25c46d0214d5a2091092e7908907dcaecd6a962cde0ac700acee222b6f3 AS build
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake python3 curl ca-certificates pkg-config bzip2 \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /source
COPY . .
RUN python3 scripts/build_dependencies.py --scratch /work \
    && cmake -S . -B /build -DCSAD_DEPS=/work/deps -DBUILD_TESTING=OFF -DCMAKE_BUILD_TYPE=Release \
    && cmake --build /build -j2 \
    && python3 scripts/check_codec_registry.py /build/cinemaseat-audio-decoder \
    && cmake --install /build --prefix /runtime \
    && mkdir -p /runtime/lib /runtime/share/licenses \
    && cp -a /work/deps/lib/libavcodec.so* /work/deps/lib/libavutil.so* /runtime/lib/ \
    && find /usr/lib -name libstdc++.so.6 -exec cp -L {} /runtime/lib/ \; \
    && find /usr/lib -name libgcc_s.so.1 -exec cp -L {} /runtime/lib/ \; \
    && cp VERSION dependencies/manifest.json THIRD_PARTY_NOTICES.md /runtime/share/ \
    && cp /work/ffmpeg-source/*/COPYING.LGPLv2.1 /runtime/share/licenses/FFmpeg-LGPL-2.1.txt \
    && cp /work/boost-source/*/LICENSE_1_0.txt /runtime/share/licenses/Boost-1.0.txt

FROM debian:bookworm-slim@sha256:99c0b25c46d0214d5a2091092e7908907dcaecd6a962cde0ac700acee222b6f3
LABEL org.opencontainers.image.title="CinemaSeat Audio Decoder" \
      org.opencontainers.image.version="0.1.1"
COPY --from=build /runtime /opt/csad
ENV LD_LIBRARY_PATH=/opt/csad/lib
USER 10001:10001
EXPOSE 9078
HEALTHCHECK --interval=30s --timeout=3s --start-period=10s --retries=3 \
    CMD ["/opt/csad/bin/cinemaseat-audio-decoder", "--healthcheck"]
ENTRYPOINT ["/opt/csad/bin/cinemaseat-audio-decoder"]
