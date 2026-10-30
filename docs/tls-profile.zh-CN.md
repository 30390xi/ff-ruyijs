# macOS 的 Firefox 100–159 TLS 配置

监听端抓包没有变。本说明里的 HTTP/2 和页面测量在分支 `tls/necko-core-arm64`，还没提交。测试过程在 `docs/tls-profile-report.zh-CN.md`。

## 决策

Mac 的 TLS 开关沿用 Windows 0.2.1 wheel 的原生入口。`oneshot.py` 导入 `necko.adapters.HTTPAdapter`。在那份 wheel 里，这个适配器组装 `necko_tls_profile`，再调用 `necko_core` 的 `necko_tls_open_profile`。版本来自 `NECKO_FIREFOX`。官方 Firefox 树和公开的 Gitee 仓库里都没有这份 C。Gitee 上只有 README。

本轮按这份 ABI 和 donor NSS 3.131 头文件，在 `native/necko_tls/necko_tls.c` 写了 arm64 适配器，编成 `libnecko_core.dylib`。配置表只剩 `native/necko_tls/necko_win/_tls_profiles.py`，与 Windows wheel 里的同名文件逐字节相同（4883 字节，MPL-2.0）。`native/necko_tls/_tls_profiles.py` 那份重复文件已删除。`hello_probe.py` 加载 `necko_win` 里的这一份。Mac 不把 Firefox 100–159 写成 xpcshell 偏好，也不在 JavaScript 里拼 ClientHello。

这个库是套在读写回调上的 TLS 过滤器。直连 HTTPS 的 HTTP/2 仍用 hyper-h2，铺在 wheel 的 `_nss.TLS` 上。连接建立在 `native/necko_tls/necko_win/_wire.py`，是 0.2.1 打开路径的一小段拷贝。它不是整份 `_http2.py`，也不导入 HTTP/3 模块。调用方没给的请求头，由 `necko_win/_firefox.py`（0.2.1 的 `_firefox.py`）按这个 `tls=` 补上。调用方写了的头优先。同一份文件提供 HTTP/2 SETTINGS、优先级树开关和 HPACK 编码器策略。这份文件里的 HTTP/3 字段不用。`wire_headers` 会找 `ruyijs_necko.wire_fields.WireFields`，`ImportError` 时继续。Mac helper 不带 `wire_fields.py`。表里的 User-Agent 仍是 wheel 里的 Windows 字符串。`configure_fingerprint` 仍是 navigator 那一层。ALPN `h2` 的版本码是 20。ALPN 为 `http/1.1` 时，同一条套接字写 HTTP/1.1，版本码 11，包括 Content-Length、分块正文，以及跳过的 1xx。这个解析器做过单元测试。没有对活的 HTTP/1.1 源打开过。其他 ALPN 会关掉套接字，并记成表示不了。

直连 HTTPS 在 `necko_tls_nss_version()` 为 `3.131` 时走这个库。`python/ruyijs/_context.py` 只在 `NECKO_CORE_LIBRARY` 未设置时写入它。先找 `dist/necko_tls/libnecko_core.dylib`，再找 `dist/necko/libnecko_core.dylib`。暂存树的 NSS 同级库已经在 `dist/necko`，dylib 的 rpath 是 `@loader_path`。这次没有重打运行时压缩包，`macos.url` 仍为空。oneshot 客户端在环境变量指向的文件存在时用它，否则沿父目录找同样两个位置，最后才看旁边的 `libnecko_core.dylib`。证书校验用 wheel 里的 cryptography 校验器和 Firefox 根证书。校验失败不回退到 xpcshell。`https://expired.badssl.com/` 抛出 `SSLCertVerificationError`，失败种类是 `verify`。oneshot 帧里没有客户端证书。`fetch(..., cert=)` 在连接前抛出，路由只在这种表示不了的情况、不支持的 ALPN，或 HTTP/2 GOAWAY 说明对端要求 HTTP/1.1 时才回退。代理、非 HTTPS、库不存在，或 NSS 版本不同，仍走 donor xpcshell，那边的校验器仍是 mozpkix。这次运行里 `RUYIJS_HTTP_PROXY` 没有设置，所以代理路径没有重测。`necko_fetch.js` 仍然不读 `NECKO_FIREFOX`。

## 直接库抓包

`native/necko_tls/hello_probe.py` 加载上面的配置表，把库连到本机 TCP 监听端，只收第一条 TLS 记录。监听端随后关闭，所以 `necko_tls_open_profile` 返回 -1，错误文本是 `handshake (PR_END_OF_FILE_ERROR)`。这是预期结果。`necko_tls_nss_version()` 为 `3.131`。这次复测的 stderr 是空的。

| tls | 套件个数 | 组个数 | key share | 字节 | SCT | ECH | 证书压缩算法个数 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 100 | 17 | 6 | 2 个：32、65 | 517 | 无 | 无 | 0 |
| 159 | 15 | 5 | 3 个：1216、32、65 | 1832 | 有 | 有 | 3 |

159 的个数和 key share 长度落在下面 156–159 那一组。100 离开了 15 个套件、5 个组、3 个 key share。监听端的 1832 字节与 xpcshell 页面抓包的 1869 不同，两份握手在这里不当成逐字节相同。对照的是个数和 key share 布局。

同一份库对 `https://example.com/` 做完握手，证书校验开着，100 和 159 都返回状态 200、ALPN `h2`、正文 577 字节。100 的记录仍是 517 字节。这次活连接上 159 的记录是 1834 字节。

## Windows wheel 实际在做什么

已发布的 `ruyijs` 0.2.1 `win_amd64` wheel 带了 `necko` 0.8.2。`js.exe` 里有 UTF-16 文本 `ruyijs_necko` 和 `oneshot`，没有 `NECKO_FIREFOX`，也没有 `necko_tls_open_profile`。外壳启动 Python helper，变量由 helper 读取。

相关的 `ruyihttp` 0.9.0 wheel 里，`necko_core.dll` 是 123392 字节。它导出 `necko_tls_open`、`necko_tls_open_profile`、`necko_tls_nss_version`，以及读写、ALPN、套件和关闭。`_nss.py` 在 `necko_tls_nss_version()` 不是 `3.131` 时直接拒绝运行。请求头、HTTP/2 设置和 HTTP/3 配置在 `_firefox.py` 的另一张表里，不是偏好。

`_tls_profiles.py` 为 4883 字节，在 `ruyihttp` 0.9.0 和 ruyijs wheel 中逐字节相同。它是 7 组，不是 60 份互不相关的握手：

| 版本 | 相对上一组的变化 |
| --- | --- |
| 100–118 | 17 个套件值、6 个组、1 个额外 key share，没有 GREASE ECH，没有证书压缩，没有 SCT |
| 119–131 | 打开 GREASE ECH |
| 132–134 | 混合组放在最前，2 个额外 key share，打开证书压缩 |
| 135–150 | 打开 SCT |
| 151–153 | 套件列表少 1 个值（剩 16 个） |
| 154–155 | 套件列表再少 1 个值（剩 15 个） |
| 156–159 | 去掉 2 个有限域组（剩 5 个组） |

上表的个数是该文件里元组的长度。文件不保存抓到的 JA3 字符串。文件头写明这些元组由 Firefox 发布标签生成，并对照过官方构建的握手。

## 这棵 macOS 树实际在做什么

`python/ruyijs/_context.py` 把 `tls` 写进子进程环境 `NECKO_FIREFOX`，变量未设置时再按上面的查找写入 `NECKO_CORE_LIBRARY`。`src/resource_network/necko_fetch.js` 仍然不读这个变量。`install_network_helper` 把 `necko_core_client.py` 复制为 `necko/core_client.py`，并把 `native/necko_tls/necko_win/` 里每个非字节码文件复制到 helper 的导入路径。这个目录放着 `_nss.py`、`_tls_profiles.py`、`_firefox.py`、`_wire.py`、`firefox-suffixes.dat` 和 Firefox 根证书。请求头表导入 `publicsuffixlist`，并读取旁边的后缀文件。helper 还要导入 `h2` 和 `cryptography`。`tools/bootstrap_macos_helper.py` 把 `publicsuffixlist` 和这两个包列在一起。已有的 venv 保留。缺的导入只从 gitignore 的 `vendor/necko-derived/wheels` 用 `pip --no-index` 安装。脚本里不写代理地址。这一轮没有再跑 bootstrap。当前 helper 已经能导入 `publicsuffixlist`。

donor 的 obj-necko `XUL` 没有 `necko_tls_open_profile` 字符串。`libnss3.dylib` 含有文本 `NSS 3.131`，这正是 Windows 入口要求的版本。缺的是导出，不是 NSS 版本。

下面两段是更早的 xpcshell 页面路径，用来说明偏好选不了这些组。它们不是现在的直连 HTTPS 握手。

对本机 xpcshell 的一次本地抓取里，变量未设置以及 `100`、`120`、`159` 都是同一个握手形状：1869 字节，15 个套件值，5 个组，11 个签名值，3 个 key share（1216、32、65 字节），有 SCT，证书压缩算法列表 6 字节，ECH 扩展 282 字节，ALPN 先 `h2` 再 `http/1.1`，支持的版本是 TLS 1.3 和 TLS 1.2。这是上面 156–159 那一组的形状，即使 API 要的是 100 或 120。变量未设置时抓了两次，两次一致。

抓取方法能看见真实变化。一份用完即弃的 `necko_fetch.js` 副本把 `security.tls.version.max` 设为 3 之后，握手变成 172 字节、12 个套件值、没有 key share、没有 ECH 扩展。偏好能动这份 NSS，选不了上面那些组。那些组还会改 HTTP/2 设置和请求头，这个偏好碰不到。

本轮用现成的 `dist/js` 打开页面，没有重编外壳。`Context()` 和 `Context(tls="100")` 都打开了 `https://example.com/`，状态 200，协议 `h2`，`readyState` 为 complete。属性分别是 `159` 和 `100`。这两次导航期间 xpcshell 没有在跑。第一条连接跟着 `tls=`：159 是 15 个套件、5 个组、3 个 key share，有 SCT、有 ECH、证书压缩算法个数 3，记录 1834 字节；100 是 17、6、2，没有 SCT，没有 ECH，记录 517 字节。同一页的第二条连接个数不变，记录长度不同，所以长度不作为验收条件。这些记录都不是原来那份 1869 字节的 xpcshell 握手。

证书校验开着，对 100、119、132、135、151、154、156、159 各做一次库内 GET，都返回状态 200、ALPN `h2`。握手个数与配置表一致，156 仍在 156–159 那一组。156 的后量子签名变化只在未使用的 HTTP/3 表里，所以没有加进 TCP 握手。页面只复查了 100 和 159。这里不写已经把 60 个版本都当页面打开过。

同一个进程里，一个适配器对 `https://example.com/` 发两次 GET，只发出一个 ClientHello，两次都是状态 200。连接池的键是主机名、端口和 `tls=`，只保留 ALPN `h2`。HTTP/1.1 一次交换后关闭。失败的交换丢掉这条连接。会话票据保持打开，所以下一次握手可能更长。长度不作为验收条件。页面外壳仍是每个请求启动一次 oneshot。`--session` 才会复用这个适配器。不保存 cookie，`redirectionLimit` 仍是 0。

设置 `NECKO_CORE_HELLO_LOG` 为一个文件路径时，每次直连会追加一行 JSON，只写这些个数，不写套件值。

## 实施顺序

1. 已完成。`tools/build.py --necko-core-only --firefox-gfx-obj <obj-necko>` 编出 arm64 `libnecko_core.dylib`，链到 donor NSS 3.131。产物在 gitignore 的 `dist/necko_tls/`。不要把 Windows DLL 拷进 Mac 树。
2. 直连 HTTPS 已接上。oneshot 把没有代理的 `https` 交给这个库，代理和其他协议仍留在 xpcshell。`https://example.com/` 在 100 和 159 都返回状态 200、ALPN `h2`。库文件存在时，`tools/macos_runtime.py` 仍会把它拷进暂存目录。
3. 已完成。直接库抓包里，`tls=100` 离开 15 个套件、5 个组、3 个 key share，`tls=159` 留在这个形状。对照的是上面的组。
4. 这条套接字上的产品请求头和 SETTINGS 表已经套用。调用方的头优先。没写的头用拷到 `necko_win/_firefox.py` 的 0.2.1 `_firefox.py` 补上。这份文件里的 HTTP/3 字段不用。表里的 User-Agent 仍是 wheel 的 Windows 字符串。`configure_fingerprint` 仍是 navigator 那一层。八个簇边界（100、119、132、135、151、154、156、159）做了库内 GET：状态 200，ALPN `h2`，个数与表一致。页面只复查了 100 和 159。同一个进程里，对同一来源的两次 GET 只用一个 ClientHello。页面外壳仍是每个子资源启动一次 oneshot，除非使用 `--session`。会话票据保持打开。记录长度不作为验收条件，也不声称与 donor 页面握手逐字节相同。这里不写 Mac 页面已经切换全部 60 个版本。
5. HTTP/3 等 `necko_quic`（`necko_quic_init`、`necko_quic_call`）。xpcshell 的 socket process 继续关着。

不往 `necko_fetch.js` 里加配置补丁。

## 验收

- 同一个 `tls` 值在 Mac 适配器和 Windows wheel 的 `_tls_profiles.py` 里选中同一组。直接库抓包已按这一条分开 100 和 159。
- 100 和 159 的直接库抓包在套件个数、组个数和 key share 布局上按上表分开。本轮通过。
- `Context()` 和 `Context(tls="100")` 打开 `https://example.com/` 都是状态 200、协议 `h2`，握手个数跟着 `tls=`。xpcshell 没有在跑。上面八个边界的库内 GET 也是这样。
- 校验失败留在这个库上。`https://expired.badssl.com/` 抛出 `SSLCertVerificationError`，没有返回状态码。路由只在表示不了时回退：客户端证书、不支持的 ALPN，或对端要求 HTTP/1.1。客户端证书的检查在连接前抛出。没有用过活的客户端证书服务。
- 同一进程内的同源 HTTP/2 复用通过：一个 ClientHello，两次状态 200。代理仍走 xpcshell。`RUYIJS_HTTP_PROXY` 没有设置，所以这条路径没有重测。
- 明文 `http://example.com/` 在旧客户端和路由器上、`NECKO_CORE_LIBRARY` 设置与不设置时，都从 donor xpcshell 返回 `necko 804b0033`（`NS_ERROR_UNKNOWN_SOCKET_TYPE`）。这个缺口原来就在。这一轮不加明文栈，也不打开 socket process。
- 仓库不增加 JA3 表、第二套 QUIC、Windows DLL，也不填写 `macos.url`。配置元组只放在那份 MPL 文件里。

## 否决

- 在 `necko_fetch.js` 里用偏好列表冒充 60 个版本。对照抓取说明偏好改变的是另一件事。
- 在 fetch 脚本里拼 ClientHello，或在 NSS 旁边再做一套 TLS。
- 准备 60 棵 Firefox 树。
- 为了追 HTTP/3 打开 socket process。那次运行已经以信号 11 退出。
