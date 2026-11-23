# Build environment of tilgen. Use it through run.sh, with mounts:
# tools/tilgen/run.sh, which mounts what cannot be in an image:
#   /opt/ida-pro   IDA (with its license), read-only
#   /work          the repo: tils go to /work/build/tils, and HOME and the venv to
#                  /work/build/tilgen-docker
#   /sdks          the SDK path, read-only
#   /xwin          the Windows headers (xwin splat), read-only
ARG BASE=python:3.12-slim-bookworm
FROM ${BASE}

ARG SNAPSHOT=
RUN if [ -n "$SNAPSHOT" ]; then \
        echo "deb [check-valid-until=no] http://snapshot.debian.org/archive/debian/$SNAPSHOT bookworm main" \
        > /etc/apt/sources.list && rm -f /etc/apt/sources.list.d/*; \
        fi \
        && apt-get update \
        && apt-get install -y --no-install-recommends \
        g++ \
        gcc-mingw-w64-x86-64 gcc-mingw-w64-i686 \
        git ca-certificates \
        libx11-dev libxext-dev libxrandr-dev libxinerama-dev libxcursor-dev \
        libxcomposite-dev libxrender-dev libxi-dev \
        libfreetype-dev libfontconfig-dev libasound2-dev \
        && rm -rf /var/lib/apt/lists/*

RUN test "$(ls /usr/lib/gcc/x86_64-linux-gnu)" = 12

ENV IDADIR=/opt/ida-pro
WORKDIR /work/tools
