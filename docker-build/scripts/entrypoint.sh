#!/usr/bin/env bash
# qBittorrent-aria2 交叉编译镜像入口。
# 不带参数时打印用法；带参数时透传执行。
set -euo pipefail

if [ "$#" -eq 0 ]; then
    cat <<'EOF'
qBittorrent-aria2 交叉编译镜像
--------------------------------
容器内构建脚本（均接受 <项目源码目录> <输出目录> [额外 cmake 参数]）：

  /opt/scripts/build-linux.sh    -> Linux x86_64（原生）
  /opt/scripts/build-windows.sh  -> Windows x86_64（MinGW-w64）
  /opt/scripts/build-macos.sh    -> macOS（osxcross，需 FINAL_BASE=macos-stage 构建镜像）

典型用法（在 Docker 宿主上，仓库根目录执行）：

  # Linux
  docker run --rm -it -v "$PWD":/src -v "$PWD/dist":/out \
      qbt-cross:latest /opt/scripts/build-linux.sh /src /out

  # Windows
  docker run --rm -it -v "$PWD":/src -v "$PWD/dist":/out \
      qbt-cross:latest /opt/scripts/build-windows.sh /src /out

  # macOS（镜像需以 --build-arg FINAL_BASE=macos-stage 构建）
  docker run --rm -it -v "$PWD":/src -v "$PWD/dist":/out \
      qbt-cross:latest /opt/scripts/build-macos.sh /src /out

  附加 cmake 参数直接追加在末尾，例如：-DGUI=OFF、-DTESTING=ON
EOF
    exit 1
fi

exec "$@"
