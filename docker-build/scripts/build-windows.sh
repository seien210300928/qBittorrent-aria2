#!/usr/bin/env bash
# 交叉编译 qBittorrent-aria2 -> Windows x86_64 (MinGW-w64)
# 用法: build-windows.sh <项目源码目录> <输出目录> [额外 cmake 参数...]
#   例: /opt/scripts/build-windows.sh /src /out -DGUI=OFF
set -euo pipefail
SRC="${1:?用法: build-windows.sh <项目源码目录> <输出目录> [额外 cmake 参数...]}"
OUT="${2:?缺少输出目录}"
shift 2
source /opt/scripts/env.sh
source /opt/scripts/lib-qbt.sh

BUILD="$SRC/build-windows"
mkdir -p "$OUT"
TOOLCHAIN="/opt/cross/toolchain-mingw64.cmake"
ROOTS="${QT_WIN_ROOT};${BOOST_ROOT};/opt/openssl-windows;/opt/zlib-windows;/opt/libtorrent-windows"

cmake -S "$SRC" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
    -DCMAKE_FIND_ROOT_PATH="$ROOTS" \
    -DCMAKE_PREFIX_PATH="$QT_WIN_ROOT" \
    -DQT_HOST_PATH="$QT_LINUX_ROOT" \
    -DBOOST_ROOT="$BOOST_ROOT" \
    -DLibtorrentRasterbar_DIR="$LT_WIN/lib/cmake/LibtorrentRasterbar" \
    -DOPENSSL_ROOT_DIR="$OPENSSL_WIN" \
    -DZLIB_ROOT="$ZLIB_WIN" \
    -DCMAKE_INSTALL_PREFIX="$OUT/windows" \
    "$@"

# 预生成 .qm（宿主 lrelease），防止构建期执行 Windows 的 lrelease.exe
pregen_qm "$BUILD" "$SRC" "$QT_LINUX_ROOT/bin/lrelease"
cmake --build "$BUILD" -j"${JOBS:-$(nproc)}"

bundle_windows "$BUILD" "$OUT/windows" "$QT_WIN_ROOT" "$SRC"
echo "==> Windows 构建完成"
file "$OUT/windows/qBittorrent/qbittorrent.exe"
