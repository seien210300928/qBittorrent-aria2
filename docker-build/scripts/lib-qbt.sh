#!/usr/bin/env bash
# qBittorrent 构建辅助函数（翻译文件预生成 / 各平台产物打包）。
set -euo pipefail

# 用宿主 lrelease 预生成 .qm，避免交叉构建时执行目标平台的 lrelease
# （qt_add_translation 的输出路径固定为 <build>/src/lang 与 <build>/src/webui/www/translations，
#   输出已存在且比 .ts 新时，CMake 不会重复执行目标平台的 lrelease）
pregen_qm() { # <build-dir> <src-dir> <host-lrelease>
    local build="$1" src="$2" lrelease="$3"
    local ts qm
    mkdir -p "$build/src/lang" "$build/src/webui/www/translations"
    for ts in "$src/src/lang/"*.ts; do
        [ -e "$ts" ] || continue
        qm="$build/src/lang/$(basename "${ts%.ts}").qm"
        "$lrelease" -silent -removeidentical "$ts" -qm "$qm"
    done
    for ts in "$src/src/webui/www/translations/"*.ts; do
        [ -e "$ts" ] || continue
        qm="$build/src/webui/www/translations/$(basename "${ts%.ts}").qm"
        "$lrelease" -silent -removeidentical "$ts" -qm "$qm"
    done
}

# Windows 打包：exe + Qt DLL + 插件 + OpenSSL DLL（复刻上游 CI 的产物结构；
# windeployqt 是 Windows 程序，无法在 Linux 容器内运行，故手动拷贝）
bundle_windows() { # <build-dir> <out-dir> <qt-win-root> <src-dir>
    local build="$1" out="$2" qt="$3" src="$4"
    local app="$out/qBittorrent"
    local dll p
    mkdir -p "$app/plugins/iconengines" "$app/plugins/imageformats" \
             "$app/plugins/platforms" "$app/plugins/sqldrivers" \
             "$app/plugins/styles" "$app/plugins/tls"
    cp "$build/qbittorrent.exe" "$app/"
    if [ -f "$src/dist/windows/qt.conf" ]; then
        cp "$src/dist/windows/qt.conf" "$app/"
    fi
    for dll in Qt6Core Qt6Gui Qt6Network Qt6Sql Qt6Svg Qt6Widgets Qt6Xml; do
        cp "$qt/bin/${dll}.dll" "$app/"
    done
    # MinGW 运行时 DLL（Qt mingw 包自带）
    for dll in libgcc_s_seh-1 libstdc++-6 libwinpthread-1; do
        if [ -f "$qt/bin/${dll}.dll" ]; then
            cp "$qt/bin/${dll}.dll" "$app/"
        fi
    done
    # OpenSSL DLL：Qt TLS 后端（qopensslbackend）运行时需要
    if [ -f "$OPENSSL_WIN_SHARED/bin/libssl-3-x64.dll" ]; then
        cp "$OPENSSL_WIN_SHARED/bin/libssl-3-x64.dll" "$app/"
    fi
    if [ -f "$OPENSSL_WIN_SHARED/bin/libcrypto-3-x64.dll" ]; then
        cp "$OPENSSL_WIN_SHARED/bin/libcrypto-3-x64.dll" "$app/"
    fi
    cp "$qt/plugins/iconengines/qsvgicon.dll" "$app/plugins/iconengines/"
    for p in qgif qicns qico qjpeg qsvg qtga qtiff qwbmp qwebp; do
        if [ -f "$qt/plugins/imageformats/${p}.dll" ]; then
            cp "$qt/plugins/imageformats/${p}.dll" "$app/plugins/imageformats/"
        fi
    done
    cp "$qt/plugins/platforms/qwindows.dll" "$app/plugins/platforms/"
    cp "$qt/plugins/sqldrivers/qsqlite.dll" "$app/plugins/sqldrivers/"
    if [ -f "$qt/plugins/styles/qmodernwindowsstyle.dll" ]; then
        cp "$qt/plugins/styles/qmodernwindowsstyle.dll" "$app/plugins/styles/"
    fi
    for dll in "$qt/plugins/tls/"*.dll; do
        [ -e "$dll" ] || continue
        cp "$dll" "$app/plugins/tls/"
    done
    echo "==> Windows 产物: $app"
}

# macOS 打包：把 Qt framework 与插件拷入 .app，写入 @rpath。
# 注意：本容器无法运行 macdeployqt / codesign，产物未签名；
#       分发前请在 Mac 上执行 codesign --force --deep --sign - <app>
bundle_macos() { # <build-dir> <out-dir> <qt-mac-root>
    local build="$1" out="$2" qt="$3"
    local app_name dest exe fw p intool
    if [ -d "$build/qbittorrent.app" ]; then
        app_name="qbittorrent.app"
    else
        app_name="qbittorrent-nox.app"
    fi
    dest="$out/$app_name"
    rm -rf "$dest"
    cp -R "$build/$app_name" "$dest"
    mkdir -p "$dest/Contents/Frameworks" "$dest/Contents/PlugIns/platforms" \
             "$dest/Contents/PlugIns/imageformats" "$dest/Contents/PlugIns/iconengines" \
             "$dest/Contents/PlugIns/sqldrivers" "$dest/Contents/PlugIns/styles"
    for fw in QtCore QtGui QtNetwork QtSql QtSvg QtWidgets QtXml; do
        if [ -d "$qt/lib/${fw}.framework" ]; then
            cp -R "$qt/lib/${fw}.framework" "$dest/Contents/Frameworks/"
        fi
    done
    cp "$qt/plugins/platforms/libqcocoa.dylib" "$dest/Contents/PlugIns/platforms/"
    for p in libqgif libqicns libqico libqjpeg libqsvg libqtga libqtiff libqwbmp libqwebp; do
        if [ -f "$qt/plugins/imageformats/${p}.dylib" ]; then
            cp "$qt/plugins/imageformats/${p}.dylib" "$dest/Contents/PlugIns/imageformats/"
        fi
    done
    cp "$qt/plugins/iconengines/libqsvgicon.dylib" "$dest/Contents/PlugIns/iconengines/"
    if [ -f "$qt/plugins/sqldrivers/libqsqlite.dylib" ]; then
        cp "$qt/plugins/sqldrivers/libqsqlite.dylib" "$dest/Contents/PlugIns/sqldrivers/"
    fi
    if [ -f "$qt/plugins/styles/libqmacstyle.dylib" ]; then
        cp "$qt/plugins/styles/libqmacstyle.dylib" "$dest/Contents/PlugIns/styles/"
    fi
    exe="$dest/Contents/MacOS/${app_name%.app}"
    intool="$(command -v llvm-install-name-tool || true)"
    if [ -z "$intool" ] && [ -x /usr/lib/llvm-18/bin/llvm-install-name-tool ]; then
        intool="/usr/lib/llvm-18/bin/llvm-install-name-tool"
    fi
    if [ -n "$intool" ]; then
        "$intool" -add_rpath "@executable_path/../Frameworks" "$exe" || true
        echo "==> 已为 $exe 写入 @executable_path/../Frameworks"
    else
        echo "!! 未找到 llvm-install-name-tool，未写入 rpath（应用可能找不到 Qt framework）" >&2
    fi
    echo "==> macOS 产物: $dest（未签名，请在 Mac 上执行 codesign --force --deep --sign -）"
}
