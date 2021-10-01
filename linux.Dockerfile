FROM ubuntu:20.04 as builder
ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update -y --fix-missing && \
    apt-get install -y \
    bash ca-certificates wget curl \
    git patch sed gawk xxd \
    make gcc g++ cmake automake autoconf libtool pkg-config \
    desktop-file-utils binutils libcairo2-dev libfuse-dev libssl-dev \
    squashfs-tools

RUN mkdir -p /work/src /work/build
COPY [ ".", "/work/src" ]

WORKDIR /work/build
RUN cmake /work/src -DUSE_SYSTEM_MKSQUASHFS=ON
RUN cmake --build .
RUN mkdir -p /work/dist && \
    cmake --install . --prefix /work/dist



FROM ubuntu:20.04
LABEL org.opencontainers.image.source=https://github.com/jclab-joseph/AppImageKit
ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update -y && \
    apt-get install -y \
    bash libglib2.0-0 zlib1g libpcre3 libssl1.1 \
    squashfs-tools binutils sed gawk xxd

RUN mkdir -p /opt/appimage
COPY --from=builder [ "/work/dist/", "/opt/appimage/" ]
COPY --from=builder [ "/work/build/src/runtime*", "/opt/appimage/" ]

ENV APPIMAGE_INSTALL_PREFIX=/opt/appimage
ENV APPIMAGE_APPRUN_FILE=/opt/appimage/bin/AppRun
ENV APPIMAGE_RUNTIME_FILE=/opt/appimage/runtime
