#!/usr/bin/env bash
# 构建 Windows x86_64 (MinGW-w64) 目标的 OpenSSL / zlib / libtorrent。
#   - OpenSSL 静态版 -> /opt/openssl-windows        （供 libtorrent 链接）
#   - OpenSSL 共享版 -> /opt/openssl-windows-shared （生成 libssl-3-x64.dll / libcrypto-3-x64.dll，供 Qt TLS 后端与分发）
#   - zlib 静态版    -> /opt/zlib-windows
#   - libtorrent 静态版 -> /opt/libtorrent-windows
# 全部幂等：已构建则跳过。
set -euo pipefail
source /opt/scripts/env.sh
source /opt/scripts/deps-common.sh

TOOLCHAIN="/opt/cross/toolchain-mingw64.cmake"
MINGW_PREFIX="x86_64-w64-mingw32"
ROOTS="-DCMAKE_FIND_ROOT_PATH=${QT_WIN_ROOT};${BOOST_ROOT};/opt/openssl-windows;/opt/zlib-windows;/opt/libtorrent-windows"

# ---- OpenSSL（静态，供 libtorrent 链接） ----
if [ ! -f "$OPENSSL_WIN/lib/libssl.a" ]; then
    fetch_openssl
    cd /tmp
    tar -xzf "openssl-${OPENSSL_VERSION}.tar.gz"
    cd "openssl-${OPENSSL_VERSION}"
    ./Configure mingw64 --cross-compile-prefix="${MINGW_PREFIX}-" \
        --prefix="$OPENSSL_WIN" no-shared no-tests
    make -j"$JOBS"
    make install_sw
    cd /tmp
    rm -rf "openssl-${OPENSSL_VERSION}"
    echo "==> OpenSSL (windows, static) 构建完成"
fi

# ---- OpenSSL（共享，提供 libssl-3-x64.dll / libcrypto-3-x64.dll） ----
if [ ! -f "$OPENSSL_WIN_SHARED/bin/libssl-3-x64.dll" ]; then
    fetch_openssl
    cd /tmp
    tar -xzf "openssl-${OPENSSL_VERSION}.tar.gz"
    cd "openssl-${OPENSSL_VERSION}"
    ./Configure mingw64 shared --cross-compile-prefix="${MINGW_PREFIX}-" \
        --prefix="$OPENSSL_WIN_SHARED" no-tests
    make -j"$JOBS"
    make install_sw
    cd /tmp
    rm -rf "openssl-${OPENSSL_VERSION}"
    echo "==> OpenSSL (windows, shared) 构建完成"
fi

# ---- zlib（静态） ----
if [ ! -f "$ZLIB_WIN/lib/libz.a" ]; then
    fetch_zlib
    cd /tmp
    tar -xzf "zlib-${ZLIB_VERSION}.tar.gz"
    cmake -S "/tmp/zlib-${ZLIB_VERSION}" -B "/tmp/zlib-${ZLIB_VERSION}-winbuild" -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$ZLIB_WIN" \
        -DBUILD_SHARED_LIBS=OFF \
        -DZLIB_BUILD_EXAMPLES=OFF \
        -DZLIB_BUILD_TESTING=OFF
    cmake --build "/tmp/zlib-${ZLIB_VERSION}-winbuild" -j"$JOBS"
    cmake --install "/tmp/zlib-${ZLIB_VERSION}-winbuild"
    cd /tmp
    rm -rf "zlib-${ZLIB_VERSION}" "zlib-${ZLIB_VERSION}-winbuild"
    echo "==> zlib (windows) 构建完成"
fi

# ---- libtorrent（静态） ----
if [ ! -f "$LT_WIN/lib/cmake/LibtorrentRasterbar/LibtorrentRasterbarConfig.cmake" ]; then
    clone_libtorrent /tmp/libtorrent-src
    cmake -S /tmp/libtorrent-src -B /tmp/libtorrent-win-build -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
        $ROOTS \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_CXX_STANDARD=20 \
        -DCMAKE_INSTALL_PREFIX="$LT_WIN" \
        -DBUILD_SHARED_LIBS=OFF \
        -Ddeprecated-functions=OFF \
        -DBOOST_ROOT="$BOOST_ROOT" \
        -DOPENSSL_ROOT_DIR="$OPENSSL_WIN" \
        -DZLIB_ROOT="$ZLIB_WIN"
    cmake --build /tmp/libtorrent-win-build -j"$JOBS"
    cmake --install /tmp/libtorrent-win-build
    echo "==> libtorrent (windows) 构建完成"
fi

echo "==> Windows 依赖全部就绪"
