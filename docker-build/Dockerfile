# syntax=docker/dockerfile:1
# ============================================================================
# qBittorrent-aria2 交叉编译镜像
#
#   目标：
#     - Linux   x86_64      原生编译（默认包含）
#     - Windows x86_64      MinGW-w64 交叉编译（默认包含）
#     - macOS   x86_64      osxcross 交叉编译（可选，--build-arg FINAL_BASE=macos-stage）
#
#   基础镜像使用 Ubuntu 24.04 LTS，原因：
#     - 其 MinGW GCC 13.2 与官方预编译 Qt 6.8 win64_mingw（MinGW 13.1）ABI 一致；
#     - 它是 osxcross 交叉工具链验证最充分的底座。
#   本镜像可以在任意 Docker 宿主上运行，包括 WSL Ubuntu 26。
#
#   构建（在 WSL 或任意 Linux Docker 宿主上）：
#     docker build -t qbt-cross:latest -f docker-build/Dockerfile docker-build/
#     # 需要 macOS 目标时：
#     docker build -t qbt-cross:latest -f docker-build/Dockerfile \
#         --build-arg FINAL_BASE=macos-stage docker-build/
# ============================================================================

ARG FINAL_BASE=base

# ---------------------------------------------------------------------------
# base：公共工具链 + Linux / Windows 目标
# ---------------------------------------------------------------------------
FROM ubuntu:24.04 AS base

ARG QT_VERSION=6.8.3
ARG BOOST_VERSION=1.90.0
ARG OPENSSL_VERSION=3.0.16
ARG ZLIB_VERSION=1.3.1
ARG LIBTORRENT_VERSION=2.0.11
ARG LIBTORRENT_REPO=https://github.com/arvidn/libtorrent.git
ARG AQT_VERSION=3.3.0
ARG MACOS_QT_ARCH=clang_64
ARG MACOS_ARCH=x86_64
ARG MACOS_DEPLOYMENT_TARGET=14.0

ENV DEBIAN_FRONTEND=noninteractive \
    QT_VERSION=${QT_VERSION} \
    BOOST_VERSION=${BOOST_VERSION} \
    OPENSSL_VERSION=${OPENSSL_VERSION} \
    ZLIB_VERSION=${ZLIB_VERSION} \
    LIBTORRENT_VERSION=${LIBTORRENT_VERSION} \
    LIBTORRENT_REPO=${LIBTORRENT_REPO} \
    MACOS_QT_ARCH=${MACOS_QT_ARCH} \
    MACOS_ARCH=${MACOS_ARCH} \
    MACOS_DEPLOYMENT_TARGET=${MACOS_DEPLOYMENT_TARGET}

SHELL ["/bin/bash", "-o", "pipefail", "-c"]

# 基础工具链：Linux 原生构建 + MinGW-w64 交叉 + osxcross 前置依赖
RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates curl wget git unzip xz-utils file patch \
        build-essential g++ gcc \
        cmake ninja-build pkg-config ccache \
        python3 python3-venv perl \
        libssl-dev zlib1g-dev libdbus-1-dev libgl1-mesa-dev \
        gcc-mingw-w64-x86-64 g++-mingw-w64-x86-64 binutils-mingw-w64-x86-64 \
        clang lld llvm llvm-18-tools \
        libxml2-dev libbz2-dev liblzma-dev libzstd-dev libcurl4-openssl-dev \
        autoconf automake libtool \
    && rm -rf /var/lib/apt/lists/*

# aqtinstall（Qt 包管理器），隔离在 venv 中
RUN python3 -m venv /opt/aqt \
    && /opt/aqt/bin/pip install --no-cache-dir "aqtinstall==${AQT_VERSION}" \
    && /opt/aqt/bin/aqt version

ENV PATH="/opt/aqt/bin:${PATH}"

# Boost：三个目标共用同一份源码（qBittorrent / libtorrent 仅需头文件）
RUN curl -fL --retry 3 -o /tmp/boost.tar.gz \
        "https://archives.boost.io/release/${BOOST_VERSION}/source/boost_${BOOST_VERSION//./_}.tar.gz" \
    && tar -xzf /tmp/boost.tar.gz -C /opt \
    && rm /tmp/boost.tar.gz

# Qt 6：Linux 宿主版（同时作为交叉编译时的 QT_HOST_PATH）+ Windows mingw 版
RUN aqt install-qt linux desktop "${QT_VERSION}" gcc_64 \
        -m qtsvg qttools qtimageformats -O /opt/qt \
    && aqt install-qt windows desktop "${QT_VERSION}" win64_mingw \
        -m qtsvg qttools qtimageformats -O /opt/qt

# 交叉工具链文件与全部构建脚本
COPY cmake/toolchain-mingw64.cmake /opt/cross/toolchain-mingw64.cmake
COPY scripts/ /opt/scripts/
RUN chmod +x /opt/scripts/*.sh

# 依赖库：Windows(MinGW) 与 Linux 原生 的 OpenSSL / zlib / libtorrent（静态）
RUN /opt/scripts/deps-windows.sh
RUN /opt/scripts/deps-linux.sh

# 清理编译中间产物，控制镜像体积
RUN rm -rf /tmp/libtorrent-src /tmp/libtorrent-win-build /tmp/libtorrent-linux-build

# ---------------------------------------------------------------------------
# macos-stage：osxcross + macOS 目标（仅当 FINAL_BASE=macos-stage 时构建）
# ---------------------------------------------------------------------------
FROM base AS macos-stage

ARG MACOS_SDK_VERSION=14.4
ARG MACOS_SDK_URL=https://github.com/joseluisq/macosx-sdks/releases/download/14.4/MacOSX14.4.sdk.tar.xz
ARG MACOS_SDK_SHA256=5170364da96521a8cfeb4c7b8ffa810f82bd7494bd7a93653b6054101ac6cbe7

ENV MACOS_SDK_VERSION=${MACOS_SDK_VERSION} \
    MACOS_SDK_URL=${MACOS_SDK_URL} \
    MACOS_SDK_SHA256=${MACOS_SDK_SHA256}

# osxcross：基于 clang 的 macOS 交叉工具链（含 SDK 校验）
RUN git clone --depth 1 https://github.com/tpoechtrager/osxcross.git /opt/osxcross-src \
    && curl -fL --retry 3 -o "/opt/osxcross-src/tarballs/MacOSX${MACOS_SDK_VERSION}.sdk.tar.xz" "${MACOS_SDK_URL}" \
    && echo "${MACOS_SDK_SHA256}  /opt/osxcross-src/tarballs/MacOSX${MACOS_SDK_VERSION}.sdk.tar.xz" | sha256sum -c - \
    && cd /opt/osxcross-src \
    && UNATTENDED=1 ./build.sh \
    && rm -f "tarballs/MacOSX${MACOS_SDK_VERSION}.sdk.tar.xz"

ENV PATH="/opt/osxcross-src/target/bin:${PATH}" \
    OSXCROSS_ROOT=/opt/osxcross-src/target

# Qt for macOS（与其它目标同一 6.8.3 版本的官方预编译包）
RUN aqt install-qt mac desktop "${QT_VERSION}" "${MACOS_QT_ARCH}" \
        -m qtsvg qttools qtimageformats -O /opt/qt

# 生成 macOS CMake 工具链，并构建 macOS 目标的 OpenSSL / zlib / libtorrent
RUN /opt/scripts/deps-macos.sh

# 清理编译中间产物
RUN rm -rf /tmp/libtorrent-src /tmp/libtorrent-mac-build

# ---------------------------------------------------------------------------
# final：运行时环境 + 入口
# ---------------------------------------------------------------------------
FROM ${FINAL_BASE} AS final

ENV LANG=C.UTF-8

WORKDIR /src
ENTRYPOINT ["/opt/scripts/entrypoint.sh"]
CMD ["/bin/bash"]
