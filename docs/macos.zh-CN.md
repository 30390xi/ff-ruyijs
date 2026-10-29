# macOS arm64 维护说明

这是 Mac 外壳的维护路径。Windows x64 仍然从 PyPI wheel 安装。macOS 在本机已经有 `dist/js` 和 `dist/libmozglue.dylib` 之后，可以打出本地的 `macosx_26_0_arm64` wheel。`runtime.json` 记下这份压缩包的大小和 sha256。macOS 的 URL 保持为空，这个仓库也不上传 wheel。

同一份事实的英文版在 [macos.md](macos.md)。

## 各部分放在哪

macOS 上的 ruyijs 只做外壳。SpiderMonkey 和 Necko/NSS 来自同一份官方 Firefox 树。这棵树放在本仓库外面，不提交进仓库。

| 部分 | 来源 |
| --- | --- |
| `dist/js`、`dist/libmozglue.dylib` | SpiderMonkey standalone donor，再经 `tools/build.py` |
| Necko/NSS、TLS、HTTP/2 | 浏览器 donor 编出的 `xpcshell` |
| 帧管道、进程启动、C ABI 客户端 | 本仓库 |
| Python helper venv | `vendor/necko-derived/python`，已被 gitignore。wheel 使用安装它的那个 Python |

维护者的目录是并列的两份：

```
firefox/                 官方 Firefox 源码，不属于 ruyijs
  obj-js-standalone/     SpiderMonkey standalone
  obj-necko/             编出 xpcshell 的浏览器构建
ff-ruyijs/               本仓库
```

xpcshell 的默认路径是本仓库的上一级加上 `firefox-159/obj-necko/dist/bin/xpcshell`。Firefox 目录如果换了名字，就设置 `RUYIJS_XPCSHELL`。装好的 wheel 会先找运行时里的 `dist/necko/xpcshell`，找不到才用 donor 路径。帧脚本默认是 `src/resource_network/necko_fetch.js`，由 client 向上查找仓库根得到；安装后的 client 则使用旁边的 `necko_fetch.js`。需要换位置时设置 `RUYIJS_NECKO_SCRIPT`。

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

本地 wheel 的标签是 `macosx_26_0_arm64`。外壳放在 `_runtime/dist/js`，donor 的 xpcshell 闭包复制到 `_runtime/dist/necko`。这个 wheel 不上传。重新生成压缩包用 `python3.12 -B tools/macos_runtime.py <zip路径>`，再把打印出的 size 和 sha256 写进 `runtime.json` 的 `macos`，`url` 留空。

## 一次请求怎么走

长度前缀帧和 Windows 相同。macOS 上的管道是 Unix socket。外壳启动 `python -I -B -m ruyijs_necko.oneshot`，这个进程在整个会话里保持一个 xpcshell。xpcshell 持有 TLS、HTTP/2、cookie 和重定向。`redirectionLimit` 为 0，所以 3xx 就作为这次的响应返回。Python 不保存 cookie，也不跟随重定向。

这条路径上验收过的是 HTTP/1.1 和 HTTP/2。帧冒烟只打印状态、版本码（HTTP/2 为 `20`）和正文长度。donor 的 XUL 里有 neqo，偏好名是 `network.http.http3.enable`。socket process 保持关闭时，对一个支持 HTTP/3 的源发两次请求，版本码仍是 20。打开 socket process 后，这份 xpcshell 会关掉管道，并且不写错误。不要再写一套 HTTP/3。

## 系统框架

外壳链接 CoreFoundation、CoreGraphics、CoreText、ImageIO 和 Security。SHA、AES-CBC、AES-GCM 和 HMAC 用 CommonCrypto。ECDSA 和 ECDH 用 Security.framework。GCM 的 oneshot 符号在本地声明，因为 macOS 26 的 SDK 头文件没有它们。

不要加入 OpenSSL、FreeType、fontconfig、libpng、libjpeg 或 libwebp。图像解码用 ImageIO。字体度量和字形掩码用 Core Text。SVG 路径边界用 Core Graphics。

POSIX 改动留在现有 `#ifndef _WIN32` 的非 Windows 一侧。Windows 的 `#else` 段保持原字节。

## 已经能用的部分

- Unix socket 帧、worker 的 `posix_spawn`，以及 C ABI 客户端。
- Core Text 字体度量和字形掩码。
- ImageIO 静态图：PNG、JPEG、GIF、WebP、BMP、ICO，预乘 BGRA。多帧 GIF 使用 ImageIO 已经合成好的显示帧。
- SVG 路径边界，覆盖 M、L、C、Q、A、Z。
- WebCrypto 的 SHA、AES-CBC、AES-GCM、HMAC、ECDSA、ECDH。ECDH 的参数字典分配失败时返回 false，调用方抛出 `OperationError`。
- 经 donor xpcshell 的 Necko/NSS。Akamai 示例曾以 `init_real` 为真通过，协议是 HTTP/2。

适配器重新链接之后，CreepJS 再次通过，包含 `svg` 和 `canvas 2d`，没有失败项。

## 已知差异

这些表面现在调用 donor 在 macOS 上用的同一套系统接口和 Firefox 代码，不是另一套图像或字体栈。

| 表面 | macOS 行为 |
| --- | --- |
| 多帧 GIF | ImageIO 的显示帧。disposal 已经应用过，所以 Windows 上被清成透明的帧，在这里可能保留背景色。解码之后，重复次数用 GIF 元数据里的值。 |
| APNG 和动画 WebP | ImageIO 取帧，然后走同一套混合。缩放用 Core Graphics，像素保持预乘。 |
| 图像色彩 | 显示器 ICC 来自 `CGDisplayCopyColorSpace` 和 `CGColorSpaceCopyICCData`，再交给 Firefox 的 qcms。没有配置文件时像素不变。 |
| Canvas 文本路径 | `CTFontCreatePathForGlyph`。`fillText` 在画字形掩码之前先建这条路径。 |
| 字体别名 | 只有 PostScript 名和字体族都匹配时，才把 `Osaka-Mono` 映射到 `Osaka`。这是 Firefox 的单字体列表，不是新写的一张表。 |
| COLR 与 CPAL | 只做 COLR 版本 0 和 CPAL 调色板 0。表字节来自 `CTFontCopyTable`。不做 COLRv1。 |
| 字形 gamma | `SystemTextGamma` 仍是 1.8，`SystemTextContrast` 仍是 1.0。可以读 `gasp` 和 `maxp`。掩码不伪造 DirectWrite 的渲染模式。 |

`src/web_media/image_codec.cpp` 和 `src/web_crypto/digest_backend.cpp` 已是 ImageIO 和 CommonCrypto 的真实现，不是桩。

DataDome 示例的首页结果是服务端评分，不是缺少的 Mac 绑定。示例的通过条件保持不变。

## 和 Windows 的比对

对照提交 `045752e` 的 Windows x64 表面。写着「同一份源码」的行，两边都还没做完。

| 表面 | Windows | macOS |
| --- | --- | --- |
| JS 上下文、DOM、指纹、时钟、Python 桥 | 已实现 | 同一套 API |
| Necko HTTP/1.1 与 HTTP/2 | 已实现 | 已验收。一条 HTTPS 帧返回状态 200、版本码 20 |
| HTTP/3 | README 把它写进 Necko 传输 | donor 的 XUL 含有 neqo。socket process 关闭时版本码停在 20；打开后这只 xpcshell 会关掉管道 |
| TLS 配置 100–159 | README 有描述。这份源码里没有选择器 | donor 的 Firefox 159 NSS ClientHello |
| WebCrypto：SHA、AES-CBC、AES-GCM、HMAC、ECDSA、ECDH | CNG | CommonCrypto 与 Security.framework |
| 静态图：PNG、JPEG、GIF、WebP、BMP、ICO | WIC | ImageIO，预乘 BGRA |
| 多帧 GIF | WIC disposal。被清掉的帧可以是透明的 | ImageIO 显示帧。同一区域可能留下背景色。重复次数来自 GIF 元数据 |
| APNG 与动画 WebP | 共用的混合 | ImageIO 取帧后再走这套混合。Core Graphics 缩放后像素仍是预乘的 |
| 图像色彩 | 显示器配置文件，再交给 qcms | `CGDisplayCopyColorSpace` 与 `CGColorSpaceCopyICCData`，再交给 qcms。没有配置文件时像素不变 |
| Canvas 文本路径与 `fillText` | DirectWrite 轮廓 | `CTFontCreatePathForGlyph`，然后画字形掩码 |
| `strokeText` | 不绘制 | 不绘制 |
| 字体别名 | 平台字体列表 | PostScript 名和字体族都匹配时，`Osaka-Mono` 映射到 `Osaka`。这一条来自 Firefox 的单字体列表 |
| COLR 与 CPAL | 版本 0，调色板 0 | 版本 0，调色板 0。表字节来自 `CTFontCopyTable` |
| 字形 gamma | DirectWrite 渲染模式 | `SystemTextGamma` 为 1.8，`SystemTextContrast` 为 1.0。可以读 `gasp` 和 `maxp` |
| WebTransport | 与 Mac 共用的 C++。树里没有 `webtransport_worker.py` | 同一份源码。真正的会话走上面那条 HTTP/3 |
| WebCodecs、音频解码、WebGPU、iframe 导航、自定义内置元素、XML、表单目标 | 同一份源码 | 只有接口，或者明确返回未实现 |
| DataDome 首页 | 服务端评分 | 服务端评分。示例的通过条件不变 |
| 安装 | PyPI 的 `win_amd64` wheel | 本地 `macosx_26_0_arm64` wheel。`runtime.json` 记下大小和 sha256。URL 为空 |
| C ABI 客户端 | 外壳旁边的 DLL | `libruyijs_client.dylib` 的安装名仍是编译机上的绝对路径。Python 按文件路径加载它 |

`image_wic.h` 里仍有一段 POSIX 桩。macOS 的解码走 `image_codec.cpp` 和 `image_gif.cpp` 里的 ImageIO。

## 后续开发计划

外壳按 `macosx_26_0_arm64` 链接。Linux、Intel Mac，以及低于 macOS 26 的系统不在这份计划里。新行为只接到已经在用的 Firefox donor 或系统框架：ImageIO、Core Text、Core Graphics、CommonCrypto、Security。

1. HTTP/3。继续用 donor 的 socket process，并从这份构建已经写出的日志里记下 xpcshell 关掉管道的原因。验收结果是版本码 30。这只 xpcshell 如果带不起那个进程，就记下来并停在这里。WebTransport 跟着这个结果走。
2. TLS 配置。先找到 Windows 上已有的 100–159 选择器，再考虑 Mac 偏好。只有 Firefox 159 donor 里已经带有这些配置时，才在 macOS 上暴露它。在那之前，Mac 的 ClientHello 就是这份 donor 的 NSS。
3. C 客户端安装名。`libruyijs_client.dylib` 由本仓库编出。把它的安装名改成 `@rpath/libruyijs_client.dylib`，并保留 `@executable_path`。XUL、xpcshell 和 NSS 的安装名保持 Firefox 写好的样子。
4. 图像像素。继续用 ImageIO。可以用一条夹具记下 GIF 背景色差异。只有 ImageIO 已经提供对应 disposal 时才改像素。
5. 字体别名。只增加 `gfxMacPlatformFontList.mm` 里 `InitAliasesForSingleFaceList` 已经列出的名字。
6. 发布。在真实的压缩包地址出现之前，`runtime.json` 的 `macos.url` 保持为空。COLRv1 等到 Windows 的解析越过版本 0 再做。

## 不要提交的东西

不要提交 Firefox 树、两份 objdir、`dist/js`、helper venv、macOS 运行时压缩包、cookie、token、代理凭据或响应正文。`runtime.json` 的 macOS 对象可以记下大小和 sha256。在真实下载地址出现之前，URL 保持为空。

## 报告 macOS 问题

请带上 ruyijs 的提交、macOS 版本、Xcode 版本、Python 版本、Firefox 标签，以及构建用的是哪一份 objdir。
