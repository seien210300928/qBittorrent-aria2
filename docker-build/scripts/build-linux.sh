#!/usr/bin/env bash
# 编译 qBittorrent-aria2 -> Linux (x86_64, 原生)
# 用法: build-linux.sh <项目源码目录> <输出目录> [额外 cmake 参数...]
#   例: /opt/scripts/build-linux.sh /src /out -DGUI=OFF
set -euo pipefail
SRC="${1:?用法: build-linux.sh <项目源码目录> <输出目录> [额外 cmake 参数...]}"
OUT="${2:?缺少输出目录}"
shift 2
source /opt/scripts/env.sh
source /opt/scripts/lib-qbt.sh

BUILD="$SRC/build-linux"
mkdir -p "$OUT"

cmake -S "$SRC" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="$QT_LINUX_ROOT" \
    -DBOOST_ROOT="$BOOST_ROOT" \
    -DLibtorrentRasterbar_DIR="$LT_LINUX/lib/cmake/LibtorrentRasterbar" \
    -DCMAKE_INSTALL_PREFIX="$OUT/linux" \
    -DCMAKE_INSTALL_RPATH_USE_LINK_PATH=ON \
    "$@"
cmake --build "$BUILD" -j"${JOBS:-$(nproc)}"
cmake --install "$BUILD"

echo "==> Linux 构建完成"
file "$OUT/linux/bin/qbittorrent" "$OUT/linux/bin/qbittorrent-nox" 2>/dev/null || true
