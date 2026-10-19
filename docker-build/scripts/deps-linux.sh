#!/usr/bin/env bash
# 构建 Linux 原生目标的 libtorrent（静态），安装到 /opt/libtorrent-linux。
# 幂等：已构建则直接退出。
set -euo pipefail
source /opt/scripts/env.sh
source /opt/scripts/deps-common.sh

PREFIX="$LT_LINUX"
if [ -f "$PREFIX/lib/cmake/LibtorrentRasterbar/LibtorrentRasterbarConfig.cmake" ]; then
    echo "==> libtorrent (linux) 已构建于 $PREFIX"
    exit 0
fi

clone_libtorrent /tmp/libtorrent-src
cmake -S /tmp/libtorrent-src -B /tmp/libtorrent-linux-build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_STANDARD=20 \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DBUILD_SHARED_LIBS=OFF \
    -Ddeprecated-functions=OFF \
    -DBOOST_ROOT="$BOOST_ROOT"
cmake --build /tmp/libtorrent-linux-build -j"$JOBS"
cmake --install /tmp/libtorrent-linux-build
echo "==> libtorrent (linux) 构建完成"
