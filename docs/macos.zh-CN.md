# macOS arm64 维护说明

这是 Mac 外壳的维护路径。Windows x64 仍然从 PyPI wheel 安装。macOS 不是这条路：`runtime.json` 里没有 macOS 文件，`setup.py` 只在本机已经有 `dist/js` 和 `dist/libmozglue.dylib` 时才把它们打进 wheel。

同一份事实的英文版在 [macos.md](macos.md)。

## 各部分放在哪

macOS 上的 ruyijs 只做外壳。SpiderMonkey 和 Necko/NSS 来自同一份官方 Firefox 树。这棵树放在本仓库外面，不提交进仓库。

| 部分 | 来源 |
| --- | --- |
| `dist/js`、`dist/libmozglue.dylib` | SpiderMonkey standalone donor，再经 `tools/build.py` |
| Necko/NSS、TLS、HTTP/2 | 浏览器 donor 编出的 `xpcshell` |
| 帧管道、进程启动、C ABI 客户端 | 本仓库 |
| Python helper venv | `vendor/necko-derived/python`，已被 gitignore |

维护者的目录是并列的两份：

```
firefox/                 官方 Firefox 源码，不属于 ruyijs
  obj-js-standalone/     SpiderMonkey standalone
  obj-necko/             编出 xpcshell 的浏览器构建
ff-ruyijs/               本仓库
```

xpcshell 的默认路径是本仓库的上一级加上 `firefox-159/obj-necko/dist/bin/xpcshell`。Firefox 目录如果换了名字，就设置 `RUYIJS_XPCSHELL`。帧脚本默认是 `src/resource_network/necko_fetch.js`，由已安装的 client 向上查找仓库根得到。需要换位置时设置 `RUYIJS_NECKO_SCRIPT`。

standalone 的 objdir 用 `tools/build.py --donor-obj` 传入。对应的环境变量是 `RUYIJS_DONOR_OBJ`。只在这一次构建里传入。不要把它写进长期的 shell 配置。

## 编译 donor

使用 Firefox 标签 `FIREFOX_159_0_RELEASE`。两份 objdir 都保留，以后的重编在原目录里继续。

SpiderMonkey standalone 的 `mozconfig-js`：

```
mk_add_options MOZ_OBJDIR=@TOPSRCDIR@/obj-js-standalone
ac_add_options --enable-project=js
ac_add_options --enable-optimize
ac_add_options --disable-debug
ac_add_options --disable-tests
ac_add_options --enable-rust-simd
```

给 xpcshell 用的浏览器 donor，`mozconfig-necko`：

```
mk_add_options MOZ_OBJDIR=@TOPSRCDIR@/obj-necko
ac_add_options --enable-application=browser
ac_add_options --enable-optimize
ac_add_options --disable-debug
ac_add_options --disable-tests
ac_add_options --disable-nodejs
ac_add_options --enable-rust-simd
ac_add_options --without-wasm-sandboxed-libraries
```

`--disable-nodejs` 会让 newtab 的包编不完。helper 不用这个包。`obj-necko/dist/bin/xpcshell` 能启动就可以停。链接出来的 XUL 是 `obj-necko/dist/bin/XUL`。

helper 关闭 socket process。client 设置 `MOZ_DISABLE_SOCKET_PROCESS=1`。`necko_fetch.js` 把 `network.process.enabled` 和 `network.http.network_access_on_socket_process.enabled` 设为 false。这些都保持原样。

## 编译外壳和 helper

helper venv 使用 Python 3.12。`tools/bootstrap_macos_helper.py` 先在 `PATH` 里找 `python3.12`，再看 `~/.local/bin`、Homebrew 和 `/usr/local/bin`。都没有就停下来。`vendor/necko-derived/python/pyvenv.cfg` 已经存在时，脚本不动这个 venv。

在仓库根目录执行，链接外壳时带上 `MACOSX_DEPLOYMENT_TARGET=26.0`：

```sh
python3.12 -B tools/bootstrap_macos_helper.py
python3.12 -B tools/build.py --donor-obj ../firefox-159/obj-js-standalone
```

如果只有通过代理才能访问 PyPI，先在环境里导出那个代理，再运行 bootstrap。脚本里不写代理地址。

bootstrap 安装帧适配器仍然要导入的三个包：`requests`、`brotli`、`zstandard`。TLS 由 donor xpcshell 完成。`requests` 只负责准备 Windows 上 oneshot 已经在用的那一帧。

`tools/build.py` 随后把已跟踪的源码复制进 venv：

| 源码 | 安装位置 |
| --- | --- |
| `src/resource_network/necko_oneshot.py` | `ruyijs_necko/oneshot.py` |
| `src/resource_network/necko_ruyi_init.py` | `ruyijs_necko/__init__.py` |
| `src/resource_network/necko_timing.py` | `ruyijs_necko/timing.py` |
| `src/resource_network/necko_client.py` | `necko/client.py` |
| `src/resource_network/necko_adapters.py` | `necko/adapters.py` |
| `src/resource_network/necko_pkg_init.py` | `necko/__init__.py` |

Windows 上同一次构建只复制 `oneshot.py`。Darwin 的 venv 不存在时，外壳仍然完成链接，也不会另造一个发布包。

外壳二进制是 `dist/js`。跑示例时把 `RUYIJS_SHELL` 指到它，并把 `python/` 放进 `PYTHONPATH`。客户端库是 `libruyijs_client.dylib`。

需要本地 wheel 时，`setup.py` 会复制 `dist/`，标签是 `macosx_26_0_arm64`。这个 wheel 不从 `runtime.json` 发布。

## 一次请求怎么走

长度前缀帧和 Windows 相同。macOS 上的管道是 Unix socket。外壳启动 `python -I -B -m ruyijs_necko.oneshot`，这个进程在整个会话里保持一个 xpcshell。xpcshell 持有 TLS、HTTP/2、cookie 和重定向。`redirectionLimit` 为 0，所以 3xx 就作为这次的响应返回。Python 不保存 cookie，也不跟随重定向。

这条路径上验收过的是 HTTP/1.1 和 HTTP/2。帧冒烟只打印状态、版本码（HTTP/2 为 `20`）和正文长度。HTTP/3 还不是 macOS 上验收过的结果。不要再写一套 HTTP/3。

## 系统框架

外壳链接 CoreFoundation、CoreGraphics、CoreText、ImageIO 和 Security。SHA、AES-CBC、AES-GCM 和 HMAC 用 CommonCrypto。ECDSA 和 ECDH 用 Security.framework。GCM 的 oneshot 符号在本地声明，因为 macOS 26 的 SDK 头文件没有它们。

不要加入 OpenSSL、FreeType、fontconfig、libpng、libjpeg 或 libwebp。图像解码用 ImageIO。字体度量和字形掩码用 Core Text。SVG 路径边界用 Core Graphics。

POSIX 改动留在现有 `#ifndef _WIN32` 的非 Windows 一侧。Windows 的 `#else` 段保持原字节。

## 已经能用的部分

- Unix socket 帧、worker 的 `posix_spawn`，以及 C ABI 客户端。
- Core Text 字体度量和字形掩码。
- ImageIO 静态图：PNG、JPEG、GIF、WebP、BMP、ICO，只取第一帧，预乘 BGRA。
- SVG 路径边界，覆盖 M、L、C、Q、A、Z。
- WebCrypto 的 SHA、AES-CBC、AES-GCM、HMAC、ECDSA、ECDH。ECDH 的参数字典分配失败时返回 false，调用方抛出 `OperationError`。
- 经 donor xpcshell 的 Necko/NSS。Akamai 示例曾以 `init_real` 为真通过，协议是 HTTP/2。

CreepJS 曾在后来那次只改 crypto 注释之前的外壳上通过，其中包含 `svg` 和 `canvas 2d`。真正重新链接之后再跑一次。

## 仍然留空的部分

这些 POSIX 入口返回 false、空表或常量，并保留 `RUYIJS_POSIX_STUB` 标记。CreepJS 没有要求它们，所以这次移植不实现：

| 表面 | 文件 | macOS 行为 |
| --- | --- | --- |
| 动图合成 | `src/web_media/image_composition.cpp` | `ComposeImageAnimation` 返回 false |
| GIF 帧合成 | `src/web_media/image_gif.cpp` | 合成留在 Windows 路径 |
| 图像色彩变换 | `src/web_media/image_color.cpp` | `ImageColorTransform` 为空 |
| Canvas 文本路径 | `src/web_geometry/canvas_text_path.cpp` | 只有空文本返回 true |
| 字体别名 | `src/web_geometry/font_aliases.cpp` | 别名表为空 |
| COLR 字形掩码 | `src/web_geometry/glyph_color.cpp` | 返回 false |
| COLR 调色板 | `src/web_geometry/glyph_palette.cpp` | 返回 false |
| 字形 gamma 与对比度 | `src/web_geometry/glyph_style.cpp` | 固定 gamma 1.8、对比度 1.0 |

`src/web_media/image_codec.cpp` 和 `src/web_crypto/digest_backend.cpp` 已是 ImageIO 和 CommonCrypto 的真实现，不是桩。

DataDome 示例的首页结果是服务端评分，不是缺少的 Mac 绑定。示例的通过条件保持不变。

## 不要提交的东西

不要提交 Firefox 树、两份 objdir、`dist/js`、helper venv、cookie、token、代理凭据或响应正文。在真实的 macOS 压缩包和它的 sha256 还没有之前，不要往 `runtime.json` 里加 macOS 地址。

## 报告 macOS 问题

请带上 ruyijs 的提交、macOS 版本、Xcode 版本、Python 版本、Firefox 标签，以及构建用的是哪一份 objdir。
