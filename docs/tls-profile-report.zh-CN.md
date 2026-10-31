# TLS 配置实测报告

xpcshell 页面测量对应提交 `d170a06`。直接库、HTTP/2 和页面切换在分支 `tls/necko-core-arm64`，还没提交。决策见 `docs/tls-profile.zh-CN.md`。本报告只记测试观察到的形状和调用关系，不记录套件序列或 JA3 字符串。

## 结论

`Context(tls=)` 会把 `100`、`120`、`159` 写进 xpcshell 的 `NECKO_FIREFOX`。xpcshell 能读到这个变量，偏好不跟着变，抓到的 ClientHello 稳定字段也不跟着变。三次版本和一次未设置得到同一份 159 形状。把 `security.tls.version.max` 改成 3 之后，同一条抓取路径立刻变成另一份握手，所以“没变化”不是抓取失效。

Windows 0.2.1 wheel 的开关不在 `js.exe` 里。它在随包的 `necko` 0.8.2：`necko_tls_open_profile`。donor 的 `XUL` 没有该符号。本轮补上了 arm64 适配器，并让直连 HTTPS 走这个库。下面的 xpcshell 小节是切换前的测量。

## 对象

- 仓库提交 `d170a06`，`main` 与 `release/macos` 相同，工作区在测试前是干净的。
- 页面外壳是这份树编出的 `dist/js`。
- TLS 进程是 Firefox 159 donor 的 obj-necko `xpcshell`。`libnss3.dylib` 含有文本 `NSS 3.131`。
- 抓取脚本是 `src/resource_network/necko_fetch.js`。代理环境已清空，`RUYIJS_HTTP_PROXY` 设为空字符串。
- Windows 对照是 PyPI 上的 `ruyijs` 0.2.1 `win_amd64` wheel，以及 `ruyihttp` 0.9.0 `win_amd64` wheel。两份都只做静态阅读，没有在 Mac 上加载 DLL。

## 1. 变量进了进程，偏好没变

用 donor `xpcshell` 跑了一小段只打印环境和几个偏好的脚本。

| 启动环境 | `NECKO_FIREFOX` | `security.tls.version.min` | `security.tls.version.max` | `network.http.http2.enabled` | `network.http.http3.enable` | `security.tls.ech.grease_probability` |
| --- | --- | --- | --- | --- | --- | --- |
| 未设置 | 空 | 3 | 4 | true | true | 100 |
| `120` | `120` | 3 | 4 | true | true | 100 |

这段脚本在打印之后以信号 11 退出（进程返回码 -11）。数值来自退出前的输出。产品路径的 `Context` 测试没有这个退出。`http3.enable` 是裸 xpcshell 的默认值。正式的 `necko_fetch.js` 仍会关掉 socket process，下面的页面测试停在 `h2`。

## 2. 四次版本请求的握手稳定字段相同

本地只收 TCP 上的第一条 TLS 记录，不完成握手。helper 每次都在发出 ClientHello 之后返回同一状态文本 `necko 804b000d`。监听端不是 TLS 服务端，这个失败是预期的，而且对照实验也是这个状态。

比较时去掉随机数和 key share 的公钥字节，GREASE 位置按类型折叠。未设置抓了两次，用来确认折叠后的字段稳定。

| 用例 | 字节 | 套件值个数 | 组个数 | 签名值个数 | key share 个数和长度 | SCT | 证书压缩算法列表 | ECH 扩展长度 | ALPN | 支持的版本 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 未设置，两次 | 1869 | 15 | 5 | 11 | 3 个：1216、32、65 | 有 | 6 字节 | 282 | `h2`, `http/1.1` | TLS 1.3, TLS 1.2 |
| `100` | 1869 | 15 | 5 | 11 | 3 个：1216、32、65 | 有 | 6 字节 | 282 | `h2`, `http/1.1` | TLS 1.3, TLS 1.2 |
| `120` | 1869 | 15 | 5 | 11 | 3 个：1216、32、65 | 有 | 6 字节 | 282 | `h2`, `http/1.1` | TLS 1.3, TLS 1.2 |
| `159` | 1869 | 15 | 5 | 11 | 3 个：1216、32、65 | 有 | 6 字节 | 282 | `h2`, `http/1.1` | TLS 1.3, TLS 1.2 |

四次的折叠字段相同。1216 字节的 key share 是这份 159 donor 的混合密钥，不是 Firefox 100 那一组会带的布局。

## 3. 对照：一个真偏好会改变握手

复制 `necko_fetch.js` 到临时文件，在原有 socket process 偏好之后增加 `security.tls.version.max = 3`。不改仓库。

| 用例 | 字节 | 套件值个数 | key share | ECH | 支持的版本扩展 |
| --- | --- | --- | --- | --- | --- |
| 上面的默认形状 | 1869 | 15 | 3 个 | 282 字节 | TLS 1.3 和 TLS 1.2 |
| `version.max = 3` | 172 | 12 | 没有 | 没有 | 没有这份扩展 |

抓取路径能分辨两份 NSS 行为。`NECKO_FIREFOX` 不在其中。

## 4. 产品 API

用源码树的 `python/ruyijs`，外壳为 `dist/js`，不设置 donor 环境，代理环境清空。

| 调用 | `context.tls` | `example.com` 状态 | 协议 | `readyState` |
| --- | --- | --- | --- | --- |
| `Context()` | `159` | 200 | `h2` | complete |
| `Context(tls="120")` | `120` | 200 | `h2` | complete |

属性变了，页面协议没变。这和上一节的握手结果一致。

## 5. Windows wheel 的静态阅读

`ruyijs_necko/oneshot.py` 在 Windows wheel 和 Mac 源码里都是 `from necko.adapters import HTTPAdapter`。两边的包内容不同。

Windows wheel 的 `site-packages/necko` 版本字符串是 `0.8.2`。`sessions.py` 用 `firefox_version()` 解析参数，空值再读 `NECKO_FIREFOX`，默认 `159`。`_nss.py` 把配置填进一个结构体：套件、组、额外 key share 个数、签名、证书压缩、SCT、GREASE ECH 和它的长度参数，然后调用 `necko_tls_open_profile`。

`ruyihttp` 0.9.0 的 `_tls_profiles.py` 与 ruyijs wheel 里的同名文件都是 4883 字节，内容相同。`_firefox.py` 两边大小不同，下表来自 ruyijs wheel 那一份，也就是 0.2.1 产品：

- User-Agent 的 `rv` 在 110–119 固定为 109，其余跟随版本号。
- `Accept` 在 128 和 132 两个点变化。
- `Priority` 从 126 开始出现，128 再变一次。
- `Accept-Language` 从 147 起 q 值由 0.5 改为 0.9。
- `Accept-Encoding` 从 126 起加入 `zstd`。
- HTTP/2 `SETTINGS` 始终带 header table 65536、初始窗口 131072、最大帧 16384。132 起关闭 push。只有 128 和 132 带 `NO_RFC7540_PRIORITIES`。128 以外且低于 132 的版本发送旧的优先级树。
- QUIC / HTTP/3 按 106、114、119、124、130、132、135、137、139、143、145、147、155、156、159 这些点分叉，入口是 `necko_quic_init` 和 `necko_quic_call`。

切换前，Mac helper venv 的 `necko` 目录只有三个文件：`__init__.py`（170 字节）、`adapters.py`（342 字节）、`client.py`（6911 字节）。当时 `tools/build.py` 在 Darwin 上只把 `necko_client.py`、`necko_adapters.py` 和 `necko_pkg_init.py` 写到这个包上。现在还会写入 `core_client.py` 和 `necko_win`。obj-necko 的 `XUL` 全文检索没有 `necko_tls_open_profile`。`js.exe` 的 UTF-8 和 UTF-16 检索没有这个符号，也没有 `NECKO_FIREFOX`。

## 6. 抓取形状对照 Windows 的 7 组

只对照个数，不对照套件编号。

| 组 | 套件值 | 组个数 | 额外 key share 参数 | SCT | 证书压缩 | GREASE ECH |
| --- | --- | --- | --- | --- | --- | --- |
| 100–118 | 17 | 6 | 1 | 无 | 无 | 无 |
| 119–131 | 17 | 6 | 1 | 无 | 无 | 有 |
| 132–134 | 17 | 7 | 2 | 无 | 有 | 有 |
| 135–150 | 17 | 7 | 2 | 有 | 有 | 有 |
| 151–153 | 16 | 7 | 2 | 有 | 有 | 有 |
| 154–155 | 15 | 7 | 2 | 有 | 有 | 有 |
| 156–159 | 15 | 5 | 2 | 有 | 有 | 有 |
| 本次 Mac 抓取 | 15 | 5 | 观察到 3 个 key share | 有 | 有 | 有，扩展 282 字节 |

3 个 key share 对应“当前组再加 2 个额外 share”。100 和 120 在表里应是 17 个套件、6 个组、没有这份 1216 字节的混合 share。抓到的却是 156–159 那一行。GREASE ECH 的长度参数在源码结构体里是输入，不是线上扩展的 282 字节，两边不要当成同一个数。

## 直接库抓包

分支 `tls/necko-core-arm64` 上，`native/necko_tls/necko_tls.c` 按 Windows `necko_tls_open_profile` 的 ABI 调用 donor NSS 3.131。公开仓库里没有这份 C，适配器是按头文件和这份 ABI 写的。配置表只剩 `native/necko_tls/necko_win/_tls_profiles.py`，为 4883 字节，与前面那份 wheel 文件逐字节相同。`native/necko_tls/_tls_profiles.py` 那份重复文件已删除。

`hello_probe.py` 使用仓库里的这份配置表，加载 `dist/necko_tls/libnecko_core.dylib`。监听端只收第一条 TLS 记录后关闭。两次 `open_result` 都是 -1，错误文本都是 `handshake (PR_END_OF_FILE_ERROR)`。`necko_tls_nss_version()` 为 `3.131`。去掉调试输出并重编之后，stderr 为 0 字节，`pass` 为 true。

| tls | 套件个数 | 组个数 | key share | 字节 | SCT | ECH | 证书压缩算法个数 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 100 | 17 | 6 | 2 个：32、65 | 517 | 无 | 无 | 0 |
| 159 | 15 | 5 | 3 个：1216、32、65 | 1832 | 有 | 有 | 3 |

100 对上 100–118 那一行的个数。159 对上 156–159 那一行，线上 key share 是 1 加 2 个额外 share。记录 1832 字节，xpcshell 页面抓包是 1869 字节，两者保留为未解释的长度差。

## 直连 HTTP/2 与页面

证书校验开着，经同一份库 GET `https://example.com/`。100 和 159 都是状态 200、ALPN `h2`、正文 577 字节。NSS 文本为 `3.131`。100 的记录是 517 字节。活连接上 159 的记录是 1834 字节，比监听端多 2 字节，个数不变。

oneshot 对没有代理的 `https` 使用这个库，代理和其他协议仍创建 xpcshell。装进 helper venv 之后，进程内 `fetch` 的结果与上面一致：100 为 17/6/2、517 字节，159 为 15/5/3、1834 字节，响应版本码 20。这两次调用期间没有 xpcshell 进程。

接着用现成的 `dist/js`，没有重编。时钟设为 system 后导航 `https://example.com/`。

| 调用 | `context.tls` | 状态 | 协议 | `readyState` | 第一条连接的个数 |
| --- | --- | --- | --- | --- | --- |
| `Context()` | `159` | 200 | `h2` | complete | 15 个套件、5 个组、3 个 key share |
| `Context(tls="100")` | `100` | 200 | `h2` | complete | 17 个套件、6 个组、2 个 key share |

导航期间 xpcshell 没有在跑。同一页还有第二条连接，个数与第一行相同，记录长度不同，所以长度不作为验收条件。上面两段是套用请求头表之前的页面测量。个数仍然有效。

## 这一轮补上的行为

请求头和 HTTP/2 设置改用 0.2.1 wheel 的 `_firefox.py`，文件在 `necko_win/_firefox.py`。调用方没写的头按 `tls=` 补上，调用方写了的头优先。SETTINGS、优先级树开关和 HPACK 编码器策略跟着这份表。测量时表里的 HTTP/3 字段由 `libnecko_quic` 读取，不写进 TLS ClientHello，也没有拷整份 `_http3.py`。后来的 `libnecko_quic` 会把这些字段写进 QUIC ClientHello，TCP 的 ClientHello 仍然不收。细节见 `docs/closed-route.zh-CN.md`。`wire_fields.py` 不在 Mac 树上，导入失败时继续。表里的 User-Agent 仍是 wheel 的 Windows 字符串。

库查找：环境变量优先；否则先 `dist/necko_tls/libnecko_core.dylib`，再 `dist/necko/libnecko_core.dylib`。暂存目录里的 NSS 同级库本来就在 `dist/necko`。测量时没有重打压缩包，`macos.url` 当时为空。后来重打了压缩包，`macos.url` 指向维护者 fork 的 Release。

ALPN 为 `http/1.1` 时，同一条套接字写 HTTP/1.1。解析器的单元测试覆盖了 Content-Length、分块正文和跳过的 1xx。这次测量没有对活的 HTTP/1.1 源打开过。后来的直连 TLS 检查打开了 `https://www.gnu.org/`，状态 200，版本码 11。页面烟雾测试仍是 `h2`。

同一个进程里，一个适配器对 `https://example.com/` 发两次 GET：一个 ClientHello，两次状态 200。页面外壳仍是每个请求一次 oneshot，`--session` 才复用适配器。会话票据保持打开，长度不作为验收条件。不保存 cookie，`redirectionLimit` 仍是 0。

证书校验失败不回退。`https://expired.badssl.com/` 经路由抛出 `SSLCertVerificationError`，失败种类是 `verify`，没有状态码。客户端证书在连接前就表示不了，路由才会回退。没有活的客户端证书服务。

八个簇边界的库内 GET（100、119、132、135、151、154、156、159）都是状态 200、ALPN `h2`，个数与配置表一致。测量时 156 的后量子签名只在 HTTP/3 表里，不写进 TCP ClientHello。TCP 握手仍是 156–159 那一组。后来的 QUIC ClientHello 会按那份表带上这组签名。页面只复查了 100 和 159，都是状态 200、协议 `h2`、`readyState` complete，xpcshell 进程数为 0。159 的个数是 15/5/3，100 是 17/6/2。

这份报告测量时，明文 `http://example.com/` 会返回 `necko 804b0033`（`NS_ERROR_UNKNOWN_SOCKET_TYPE`）。当时没有加明文栈，也没有打开 socket process。现在的直连明文走 `libnecko_core` 的 `necko_socket_*` / `necko_exchange_*`。请求若仍落到 donor xpcshell，还是 `804b0033`。细节见 `docs/closed-route.zh-CN.md`。`RUYIJS_HTTP_PROXY` 这次没有设置，代理路径没有重测。

## 方案

见 `docs/tls-profile.zh-CN.md`。直连 HTTPS 的握手个数和产品请求头、HTTP/2 设置表都跟着 `tls=`。页面只复查了 100 和 159。八个边界是库内 GET。`necko_fetch.js` 不加偏好表。当时还没做的是 HTTP/3，以及 donor 明文 HTTP 的套接字类型。这两项后来分别接到 `libnecko_quic` 和 `libnecko_core`，见 `docs/closed-route.zh-CN.md`。记录长度不要求逐字节相同。

## 这次没有做的事

- 没有改 donor，没有重编外壳，没有打 wheel 或运行时压缩包。
- 没有打开 socket process，没有拷整份 `_http3.py`。后来的 `libnecko_quic` 不在这次报告的改动里，见 `docs/closed-route.zh-CN.md`。
- 没有把 Windows DLL 装进 Mac。
- 没有在文档里抄套件序列或 JA3。配置元组只在那份 MPL 文件里。
- 没有重测代理。
- 没有提交，没有推送，没有合并上游 PR。
- 没有声称 Mac 页面已经逐个验证全部 60 个版本。
- 这次报告没有打开活的 HTTP/1.1 源。后来的直连 TLS 检查打开了 `https://www.gnu.org/`，状态 200，版本码 11。
