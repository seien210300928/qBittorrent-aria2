#!/usr/bin/env bash
# 构建 macOS 目标的 OpenSSL / zlib / libtorrent（静态，osxcross）。
# 同时生成 /opt/cross/toolchain-macos.cmake。
# 默认 x86_64（Intel / Rosetta）；MACOS_ARCH=arm64 可切换 Apple Silicon。
# 幂等：已构建则跳过。
set -euo pipefail
source /opt/scripts/env.sh
source /opt/scripts/deps-common.sh

export PATH="/opt/osxcross-src/target/bin:${PATH}"
export MACOSX_DEPLOYMENT_TARGET="$MACOS_DEPLOYMENT_TARGET"

SDK="$(ls -d /opt/osxcross-src/target/SDK/MacOSX*.sdk 2>/dev/null | head -1 || true)"
if [ -z "$SDK" ]; then
    echo "!! 未找到 macOS SDK：请用 --build-arg FINAL_BASE=macos-stage 构建镜像" >&2
    exit 1
fi

if [ "$MACOS_ARCH" = "arm64" ]; then
    CLANG_WRAPPER="oa64"
    OPENSSL_TARGET="darwin64-arm64-cc"
    CMAKE_ARCH="arm64"
else
    CLANG_WRAPPER="o64"
    OPENSSL_TARGET="darwin64-x86_64-cc"
    CMAKE_ARCH="x86_64"
fi
# osxcross 提供的编译器包装：o64-clang / oa64-clang 等
[ -x "/opt/osxcross-src/target/bin/${CLANG_WRAPPER}-clang" ] || {
    echo "!! 缺少 osxcross 包装器 ${CLANG_WRAPPER}-clang" >&2
    exit 1
}

# 生成 macOS CMake 工具链
cat > /opt/cross/toolchain-macos.cmake <<EOF
set(CMAKE_SYSTEM_NAME Darwin)
set(CMAKE_SYSTEM_PROCESSOR ${CMAKE_ARCH})
set(CMAKE_OSX_ARCHITECTURES "${CMAKE_ARCH}")
set(CMAKE_OSX_DEPLOYMENT_TARGET "${MACOS_DEPLOYMENT_TARGET}")
set(CMAKE_OSX_SYSROOT "${SDK}")
set(CMAKE_C_COMPILER ${CLANG_WRAPPER}-clang)
set(CMAKE_CXX_COMPILER ${CLANG_WRAPPER}-clang++)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
EOF
echo "==> 已生成 /opt/cross/toolchain-macos.cmake"

ROOTS="-DCMAKE_FIND_ROOT_PATH=${QT_MAC_ROOT};${BOOST_ROOT};/opt/openssl-macos;/opt/zlib-macos;/opt/libtorrent-macos"

# ---- OpenSSL（静态） ----
if [ ! -f "$OPENSSL_MAC/lib/libssl.a" ]; then
    fetch_openssl
    cd /tmp
    tar -xzf "openssl-${OPENSSL_VERSION}.tar.gz"
    cd "openssl-${OPENSSL_VERSION}"
    CC="${CLANG_WRAPPER}-clang" ./Configure "${OPENSSL_TARGET}" \
        --prefix="$OPENSSL_MAC" no-shared no-tests
    make -j"$JOBS"
    make install_sw
    cd /tmp
    rm -rf "openssl-${OPENSSL_VERSION}"
    echo "==> OpenSSL (macos) 构建完成"
fi

# ---- zlib（静态） ----
if [ ! -f "$ZLIB_MAC/lib/libz.a" ]; then
    fetch_zlib
    cd /tmp
    tar -xzf "zlib-${ZLIB_VERSION}.tar.gz"
    cmake -S "/tmp/zlib-${ZLIB_VERSION}" -B "/tmp/zlib-${ZLIB_VERSION}-macbuild" -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE=/opt/cross/toolchain-macos.cmake \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$ZLIB_MAC" \
        -DBUILD_SHARED_LIBS=OFF \
        -DZLIB_BUILD_EXAMPLES=OFF \
        -DZLIB_BUILD_TESTING=OFF
    cmake --build "/tmp/zlib-${ZLIB_VERSION}-macbuild" -j"$JOBS"
    cmake --install "/tmp/zlib-${ZLIB_VERSION}-macbuild"
    cd /tmp
    rm -rf "zlib-${ZLIB_VERSION}" "zlib-${ZLIB_VERSION}-macbuild"
    echo "==> zlib (macos) 构建完成"
fi

# ---- libtorrent（静态） ----
if [ ! -f "$LT_MAC/lib/cmake/LibtorrentRasterbar/LibtorrentRasterbarConfig.cmake" ]; then
    clone_libtorrent /tmp/libtorrent-src
    cmake -S /tmp/libtorrent-src -B /tmp/libtorrent-mac-build -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE=/opt/cross/toolchain-macos.cmake \
        $ROOTS \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_CXX_STANDARD=20 \
        -DCMAKE_INSTALL_PREFIX="$LT_MAC" \
        -DBUILD_SHARED_LIBS=OFF \
        -Ddeprecated-functions=OFF \
        -DBOOST_ROOT="$BOOST_ROOT" \
        -DOPENSSL_ROOT_DIR="$OPENSSL_MAC" \
        -DZLIB_ROOT="$ZLIB_MAC"
    cmake --build /tmp/libtorrent-mac-build -j"$JOBS"
    cmake --install /tmp/libtorrent-mac-build
    echo "==> libtorrent (macos) 构建完成"
fi

echo "==> macOS 依赖全部就绪"
