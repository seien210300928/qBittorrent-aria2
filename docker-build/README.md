# qBittorrent-aria2 交叉编译 Docker 镜像

一套在 **WSL Ubuntu 26**（或任意 Linux Docker 宿主）中构建本项目
（qBittorrent v5_2_x fork + aria2-next 子模块）的 Docker 镜像：

| 目标 | 工具链 | 产物 |
|---|---|---|
| Linux x86_64 | 原生 GCC（镜像内编译） | `qbittorrent` / `qbittorrent-nox` |
| Windows x86_64 | MinGW-w64（GCC 13.2） | `qbittorrent.exe` + Qt DLL/插件（开箱即用的绿色目录） |
| macOS x86_64 | osxcross（clang） | `qbittorrent.app`（未签名，需在 Mac 上 codesign） |

> macOS 目标默认不包含在镜像里（osxcross + macOS SDK 体积大、构建慢），
> 用 `--build-arg FINAL_BASE=macos-stage` 开启。arm64 支持见下文。

## 目录结构

```
docker-build/
├── Dockerfile                     # 镜像定义（可参数化）
├── .dockerignore
├── README.md
├── cmake/
│   └── toolchain-mingw64.cmake    # Windows 交叉编译 CMake 工具链
└── scripts/                       # 全部构建脚本（会被 COPY 进镜像 /opt/scripts/）
    ├── entrypoint.sh              # 容器入口（打印用法 / 透传命令）
    ├── env.sh                     # 各目标路径环境变量
    ├── deps-common.sh             # 依赖下载/克隆辅助
    ├── deps-linux.sh              # Linux 目标 libtorrent（静态）
    ├── deps-windows.sh            # Windows 目标 OpenSSL/zlib/libtorrent
    ├── deps-macos.sh              # macOS 目标 OpenSSL/zlib/libtorrent + 工具链生成
    ├── lib-qbt.sh                 # 翻译预生成、三平台产物打包
    ├── build-linux.sh
    ├── build-windows.sh
    └── build-macos.sh
```

## 镜像里装了什么（版本与上游 CI 对齐）

| 组件 | 版本 | 说明 |
|---|---|---|
| 基础镜像 | ubuntu:24.04 | MinGW GCC 13.2 与 Qt mingw 版 ABI 一致；osxcross 验证最充分 |
| Qt | 6.8.3 | 三个目标同一版本：`gcc_64` / `win64_mingw` / `clang_64` |
| Boost | 1.90.0 | 头文件即够用，三目标共用一份 |
| OpenSSL | 3.0.16 | 静态（供 libtorrent）+ Windows 共享版（出 DLL 供 Qt TLS） |
| zlib | 1.3.1 | 静态 |
| libtorrent | 2.0.11 | 静态，三目标各自编译 |
| aqtinstall | 3.3.0 | Qt 安装器 |

## 一、构建镜像

先进入 WSL Ubuntu 26，把仓库挂载或拷贝到 WSL 文件系统（`/mnt/c|i/...` 较慢，
建议 `cp -r` 进 `~/` 或直接 clone）：

```bash
cd ~/qBittorrent-aria2          # 你的仓库目录
git submodule update --init --recursive   # 确保 aria2-next 子模块存在

# 默认：Linux + Windows
docker build -t qbt-cross:latest -f docker-build/Dockerfile docker-build/

# 需要 macOS 目标：加 FINAL_BASE=macos-stage（osxcross 编译约 40-60 分钟，镜像 +5GB 左右）
docker build -t qbt-cross:latest \
    --build-arg FINAL_BASE=macos-stage \
    -f docker-build/Dockerfile docker-build/
```

常用 `--build-arg`：

| 参数 | 默认 | 说明 |
|---|---|---|
| `FINAL_BASE` | `base` | `macos-stage` 时启用 macOS 目标（osxcross + macOS SDK + Qt mac） |
| `QT_VERSION` | `6.8.3` | Qt 版本（三目标一致） |
| `BOOST_VERSION` | `1.90.0` | Boost 版本 |
| `LIBTORRENT_VERSION` | `2.0.11` | libtorrent 版本（`v${版本}` tag） |
| `MACOS_ARCH` | `x86_64` | `arm64` 可出 Apple Silicon 版（需同时 `MACOS_QT_ARCH=clang_arm64`） |
| `MACOS_SDK_VERSION` / `MACOS_SDK_URL` / `MACOS_SDK_SHA256` | 14.4 | macOS SDK（默认使用社区打包的 SDK，见下方许可说明） |

> 网络慢时可用国内 Qt 镜像加速 aqt 下载：
> `--build-arg` 方式改不了 aqt 的 `-b`，可在 `Dockerfile` 里给 aqt 加
> `-b https://mirrors.ustc.edu.cn/qtproject`（aqt 支持 `-b/--base` 参数）。

## 二、编译三个平台

在 WSL 中执行（`dist` 是仓库里的产物目录，会写入 `dist/<平台>/`）：

```bash
# Linux
docker run --rm -it -v "$PWD":/src -v "$PWD/dist":/out \
    qbt-cross:latest /opt/scripts/build-linux.sh /src /out

# Windows
docker run --rm -it -v "$PWD":/src -v "$PWD/dist":/out \
    qbt-cross:latest /opt/scripts/build-windows.sh /src /out

# macOS（需要 FINAL_BASE=macos-stage 的镜像）
docker run --rm -it -v "$PWD":/src -v "$PWD/dist":/out \
    qbt-cross:latest /opt/scripts/build-macos.sh /src /out
```

所有脚本都接受末尾的额外 cmake 参数，例如：

```bash
... /opt/scripts/build-linux.sh /src /out -DGUI=OFF -DTESTING=ON
```

产物位置：

| 目标 | 输出 |
|---|---|
| Linux | `dist/linux/bin/qbittorrent[|-nox]` |
| Windows | `dist/windows/qBittorrent/`（绿色目录：exe + DLL + 插件，拷走即用） |
| macOS | `dist/macos/qbittorrent.app` |

构建完成后可在容器内/外快速验证：

```bash
file dist/linux/bin/qbittorrent          # ELF 64-bit
file dist/windows/qBittorrent/qbittorrent.exe   # PE32+ (GUI) x86-64
file dist/macos/qbittorrent.app/Contents/MacOS/qbittorrent  # Mach-O 64-bit
```

不想每次传路径，也可以进容器手动操作：

```bash
docker run --rm -it -v "$PWD":/src -v "$PWD/dist":/out qbt-cross:latest
# 容器内：source /opt/scripts/env.sh 后直接用 cmake 命令
```

## 三、注意事项与限制

### 跨平台通用
- 交叉构建时通过 `-DQT_HOST_PATH` 指向容器内的 Linux Qt，moc/uic/rcc/lrelease
  等工具全部在宿主机运行；`src/lang/*.ts` 会先用宿主 `lrelease` 预生成 `.qm`
  （qt_add_translation 的产物已存在时不会重复执行目标平台的 lrelease）。
- 镜像默认不执行 `check`（测试套件），需要时加 `-DTESTING=ON`。

### Windows
- Linux 上无法运行 `windeployqt.exe`，打包脚本按上游 CI 的清单手动拷贝
  Qt DLL、插件（platforms/sqldrivers/tls/styles/imageformats）与
  `libssl-3-x64.dll` / `libcrypto-3-x64.dll`（Qt TLS 后端运行时需要）。
- 产物目录拷到 Windows 即可运行；如需安装包（NSIS），可在 Windows 上
  用 `dist/windows/qbittorrent.nsi` 再打包。

### macOS
- **SDK 许可**：macOS SDK 来自 Apple 的 Xcode，本镜像默认下载社区打包的
  `MacOSX14.4.sdk`（joseluisq/macosx-sdks，osxcross 生态常用做法）。
  如你有合规顾虑，可自备 SDK 并用 `MACOS_SDK_URL`/`MACOS_SDK_SHA256` 覆盖。
- **未签名**：Linux 上无法运行 Apple 的 `codesign`/`hdiutil`，产物是未签名的
  `.app`。在 Mac 上执行 `codesign --force --deep --sign - qbittorrent.app`
  后再分发；本机运行不受影响。
- **arm64**：Apple Silicon 版需 `--build-arg MACOS_ARCH=arm64 --build-arg
  MACOS_QT_ARCH=clang_arm64` 重建镜像（产出 arm64 二进制；x86_64 版在
  Apple Silicon 上可经 Rosetta 2 运行）。
- 打包脚本用 `llvm-install-name-tool` 写入 `@executable_path/../Frameworks`
  rpath，并把 Qt framework 拷入 `Contents/Frameworks`。

### aria2-next 子模块
本镜像编译的是仓库主项目（qBittorrent），其 CMake 不引用 `aria2-next`
（该子模块是独立的 CMake 工程，带 vendored 依赖，`ARIA2_SUPERBUILD`）。
如需一并编译它，可以在容器里直接：
`cmake -S /src/aria2-next -B /src/aria2-next/build && cmake --build /src/aria2-next/build`
（Windows/macOS 目标需另配工具链，可按需扩展脚本）。

### 其它
- 本镜像**不改动仓库任何现有文件**：全部新增内容都位于 `docker-build/` 目录。
- WSL 挂载 `/mnt/<盘符>` 的 I/O 较慢，编译大项目建议把仓库放进 WSL 自身文件系统。
- 镜像预计 6-9GB（开 macOS 后约 11-14GB），构建耗时可观，请保证磁盘与网络。

## 四、换版本 / 重新构建依赖

依赖（libtorrent/OpenSSL/zlib）已预编译进镜像。如需换版本重编，可在容器内
先删除对应前缀再重跑 deps 脚本：

```bash
docker run --rm -it -v "$PWD":/src -v "$PWD/dist":/out qbt-cross:latest bash
# 例：重编 Windows 目标 libtorrent
rm -rf /opt/libtorrent-windows && LIBTORRENT_VERSION=2.0.12 /opt/scripts/deps-windows.sh
```

> 注意：libtorrent 需与 Boost/Qt 匹配；`2.0.11 + Boost 1.90 + Qt 6.8.3`
> 是上游 CI 的验证组合，改动前先确认兼容性。
