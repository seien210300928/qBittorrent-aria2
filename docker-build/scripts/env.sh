#!/usr/bin/env bash
# 环境变量：qbt-cross 镜像内各目标的编译路径。
# 由 build-*.sh / deps-*.sh 引用，不要直接执行。
set -euo pipefail

QT_VERSION="${QT_VERSION:-6.8.3}"
BOOST_VERSION="${BOOST_VERSION:-1.90.0}"
MACOS_QT_ARCH="${MACOS_QT_ARCH:-clang_64}"
MACOS_ARCH="${MACOS_ARCH:-x86_64}"
MACOS_DEPLOYMENT_TARGET="${MACOS_DEPLOYMENT_TARGET:-14.0}"

QT_LINUX_ROOT="/opt/qt/${QT_VERSION}/gcc_64"
QT_WIN_ROOT="/opt/qt/${QT_VERSION}/win64_mingw"
QT_MAC_ROOT="/opt/qt/${QT_VERSION}/${MACOS_QT_ARCH}"
BOOST_ROOT="/opt/boost_${BOOST_VERSION//./_}"
LT_LINUX="/opt/libtorrent-linux"
LT_WIN="/opt/libtorrent-windows"
LT_MAC="/opt/libtorrent-macos"
OPENSSL_WIN="/opt/openssl-windows"
OPENSSL_WIN_SHARED="/opt/openssl-windows-shared"
ZLIB_WIN="/opt/zlib-windows"
OPENSSL_MAC="/opt/openssl-macos"
ZLIB_MAC="/opt/zlib-macos"

export QT_VERSION BOOST_VERSION MACOS_QT_ARCH MACOS_ARCH MACOS_DEPLOYMENT_TARGET
export QT_LINUX_ROOT QT_WIN_ROOT QT_MAC_ROOT BOOST_ROOT
export LT_LINUX LT_WIN LT_MAC OPENSSL_WIN OPENSSL_WIN_SHARED ZLIB_WIN OPENSSL_MAC ZLIB_MAC
export PATH="/opt/qt/${QT_VERSION}/gcc_64/bin:/opt/aqt/bin:${PATH:-}"
