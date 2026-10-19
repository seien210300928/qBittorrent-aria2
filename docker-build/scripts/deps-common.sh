#!/usr/bin/env bash
# 依赖下载/克隆辅助函数（可被 deps-*.sh 复用，也支持在镜像内重新执行以更换版本）。
set -euo pipefail

: "${JOBS:=$(nproc)}"
: "${OPENSSL_VERSION:=3.0.16}"
: "${ZLIB_VERSION:=1.3.1}"
: "${BOOST_VERSION:=1.90.0}"
: "${LIBTORRENT_VERSION:=2.0.11}"
: "${LIBTORRENT_REPO:=https://github.com/arvidn/libtorrent.git}"

export JOBS OPENSSL_VERSION ZLIB_VERSION BOOST_VERSION LIBTORRENT_VERSION LIBTORRENT_REPO

dl() { # <url> <dest>
    local url="$1" dest="$2"
    if [ ! -f "$dest" ]; then
        echo "==> 下载 $url"
        curl -fL --retry 3 --retry-delay 2 -o "$dest" "$url"
    else
        echo "==> 已存在 $dest"
    fi
}

fetch_openssl() { dl "https://www.openssl.org/source/openssl-${OPENSSL_VERSION}.tar.gz" "/tmp/openssl-${OPENSSL_VERSION}.tar.gz"; }
fetch_zlib()    { dl "https://zlib.net/fossils/zlib-${ZLIB_VERSION}.tar.gz" "/tmp/zlib-${ZLIB_VERSION}.tar.gz"; }

clone_libtorrent() { # <dir>
    local dir="$1"
    if [ -d "$dir/.git" ]; then
        echo "==> libtorrent 已克隆于 $dir"
        return 0
    fi
    echo "==> 克隆 libtorrent v${LIBTORRENT_VERSION}"
    git clone --branch "v${LIBTORRENT_VERSION}" --depth 1 --recurse-submodules "$LIBTORRENT_REPO" "$dir"
}
