#!/usr/bin/env bash
# 交叉编译 qBittorrent-aria2 -> macOS
#   - 默认 x86_64（Intel 与 Apple Silicon 上的 Rosetta 2）
#   - 构建镜像时传 --build-arg MACOS_ARCH=arm64 --build-arg MACOS_QT_ARCH=clang_arm64 可出 arm64 版
# 用法: build-macos.sh <项目源码目录> <输出目录> [额外 cmake 参数...]
#   例: /opt/scripts/build-macos.sh /src /out -DGUI=OFF
set -euo pipefail
SRC="${1:?用法: build-macos.sh <项目源码目录> <输出目录> [额外 cmake 参数...]}"
OUT="${2:?缺少输出目录}"
shift 2
source /opt/scripts/env.sh
source /opt/scripts/lib-qbt.sh

export PATH="/opt/osxcross-src/target/bin:${PATH}"
export MACOSX_DEPLOYMENT_TARGET="$MACOS_DEPLOYMENT_TARGET"

BUILD="$SRC/build-macos"
mkdir -p "$OUT"
TOOLCHAIN="/opt/cross/toolchain-macos.cmake"
if [ ! -f "$TOOLCHAIN" ]; then
    echo "!! 缺少 $TOOLCHAIN：请用 --build-arg FINAL_BASE=macos-stage 构建镜像" >&2
    exit 1
fi
ROOTS="${QT_MAC_ROOT};${BOOST_ROOT};/opt/openssl-macos;/opt/zlib-macos;/opt/libtorrent-macos"

cmake -S "$SRC" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
    -DCMAKE_FIND_ROOT_PATH="$ROOTS" \
    -DCMAKE_PREFIX_PATH="$QT_MAC_ROOT" \
    -DQT_HOST_PATH="$QT_LINUX_ROOT" \
    -DBOOST_ROOT="$BOOST_ROOT" \
    -DLibtorrentRasterbar_DIR="$LT_MAC/lib/cmake/LibtorrentRasterbar" \
    -DOPENSSL_ROOT_DIR="$OPENSSL_MAC" \
    -DZLIB_ROOT="$ZLIB_MAC" \
    -DCMAKE_INSTALL_PREFIX="$OUT/macos" \
    "$@"

# 预生成 .qm（宿主 lrelease），防止构建期执行 macOS 的 lrelease
pregen_qm "$BUILD" "$SRC" "$QT_LINUX_ROOT/bin/lrelease"
cmake --build "$BUILD" -j"${JOBS:-$(nproc)}"

bundle_macos "$BUILD" "$OUT/macos" "$QT_MAC_ROOT"
echo "==> macOS 构建完成"
file "$OUT/macos"/*.app/Contents/MacOS/* 2>/dev/null || true
