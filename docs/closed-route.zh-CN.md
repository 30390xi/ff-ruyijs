# 0.2.1 闭源路线的语义适配

公开的 Windows 0.2.1 轮子带有 `necko_core.dll` 和 `necko_quic.dll`。开源的 oneshot 并不自己实现明文 HTTP 和 HTTP/3，而是交给随轮子发布的 `necko.adapters.HTTPAdapter`。下面只记从这两份 DLL 的导出和反汇编里收回来的行为。推断单独标出。这里不记录套件编号、JA3、凭据或本机家目录。

## 收回来的行为

明文连接是 `necko_socket_open`，它只有一条调用，进入 `necko_socket_open_cancellable`。后者使用 `WS2_32` 的 `WSAStartup`、`getaddrinfo`、`socket`、`connect`、`select`。它不调用 `WinHttp*`，也不进入 donor 的 socket process。`WinHttp*` 只属于另一组 `necko_http_perform*` 导出。

明文报文是 `necko_exchange_open`。它拼出字面量 ` HTTP/1.1\r\n`，经调用方的写回调把请求写出去。`necko_exchange_receive` 识别状态行里的 `HTTP/1.1`，也识别分块结束的 `0\r\n\r\n`。`necko_exchange_headers` 返回已经收好的头块。`necko_exchange_read` 用 `strtoul` 读分块长度。调用方在状态行是 `HTTP/1.1` 时把版本记为 11，否则记为 10。

HTTP/3 的协调是 `necko_quic_set_decoder` 加上 `necko_quic_call`。解码回调只被放进线程局部存储。`necko_quic_call` 吃一段 JSON。镜像里的命令名是 `new`、`attach`、`pump`、`events`、`auth`、`info`、`fetch`、`write`、`read`、`finish`、`cancel`、`close`。`necko_quic_version` 返回 `neqo-0.32.0/nss-3.131`。引擎在 `vendor/neqo-http3` 和 `vendor/neqo-transport`。Python 自己创建并连接 UDP 套接字，再把文件描述符交给 `attach`。

两份 DLL 都没有 socket process 或 XUL 的导入。Windows 的明文 HTTP 和 HTTP/3 不走 donor socket process。

## 本地适配

Mac 的 oneshot 仍从 `necko.adapters.HTTPAdapter` 进 `open_response`。导出名字和 JSON 命令与 Windows 相同。Darwin 加载自己编出来的库，Windows 仍加载原来的 DLL。没有代理时：

- `http://` 走 `necko_socket_*` 和 `necko_exchange_*`。这两段编进 `dist/necko_tls/libnecko_core.dylib`。库不在就失败，不回退到 xpcshell，因此这条路径不再返回 `necko 804b0033`。xpcshell 自己对明文仍会返回这个错误。
- 直连 HTTPS 仍走 `libnecko_core`。只有 `CoreClient` 真正送出的成功响应里带了 `Alt-Svc: h3="host:port"`，才把备用端点记在这个进程的会话缓存里。文法与 0.2.1 的 `clear` 和 `ma` 相同。请求头里的 `Alt-Svc` 不作为入口。同一进程里，下一次到该来源的直连请求才走 HTTP/3，版本码 30。
- HTTP/3 库是 `dist/necko_tls/libnecko_quic.dylib`。它链 Firefox 159 自带的 Neqo 0.32.0 和 donor NSS 3.131，不链 `neqo_glue`，也不另起一套 QUIC。`necko_quic_init` 调用 `nss::init()`。NSS 已经由明文/TLS 库初始化时，这次调用只记下外部状态。`necko_quic_version` 在 NSS 版本字符串包含 `3.131` 时返回 `neqo-0.32.0/nss-3.131`。Python 创建并连接 UDP 套接字，再把文件描述符交给 `attach`。证书仍由 Python 校验，`auth` 只接收接受或拒绝。校验失败是 `HTTP/3 certificate verification failed`，不回退成 200，也不交给 xpcshell。
- 页面 WebTransport 不走 `http3_fetch`。`necko_oneshot.py` 看到 `--webtransport` 就进入 `webtransport_worker.py`。`setup.py` 和 `tools/build.py` 把 `webtransport_worker.py`、`webtransport_wire.py`、`webtransport_verify.py`、`webtransport_session.py`、`webtransport_connection.py` 拷进 helper 的 `ruyijs_necko`。这条长连接在 `new` 里把 `webtransport` 设为 true，再发 0.2.1 已经在发的 JSON 命令 `webtransport`。动作 `open`、`info`、`stream`、`datagram`、`reset`、`stop`、`group`、`order`、`set_group`、`export`、`close`、`stream_stats` 调用 Neqo 0.32.0 已有的 `ClientSession` 方法。读、写、结束仍是 `read`、`write`、`finish`。事件是 `wt_negotiated`、`wt_session`、`wt_datagram`、`wt_stream`、`wt_draining`、`wt_closed`。没有 Windows 的 `necko._http3.Connection` 时用 `webtransport_connection.py`，库路径仍认 `NECKO_QUIC_LIBRARY`。证书哈希仍按 0.2.1 的 `HashContext`：ECDSA P-256，有效期不超过 14 天。不带哈希时走 PKI。普通 HTTP/3 的 `webtransport` 仍是 false。这里没有把 QUIC ClientHello 做成 60 个版本。
- 代理仍走 xpcshell。`MOZ_DISABLE_SOCKET_PROCESS` 保持为 1。打开 socket process 后，这份 xpcshell 以信号 11 退出，日志是空的。
- 缓存挂在当前进程的 adapter 上。页面外壳默认每个请求启动一次 oneshot，新进程看不到上一次的 `Alt-Svc`。只有 `--session` 把多次请求留在同一个进程里，第二次才可能是版本码 30。

`new` 读调用方已经组好的 profile 对象。Neqo 有对应 setter 的数字和布尔会写进去，包括数据报大小、QUIC bit grease、禁止迁移、SNI slicing、可靠流重置、双向和单向 `max_streams`，以及 `version2` 为真时的版本列表。组和签名留在这个对象里，不在新代码里重编码。

分块元数据长度大于等于 `0x10001` 时失败。Windows `necko_core.dll` 的比较立即数是 `0x10001`，失败文本是 `Chunk metadata too large`。能接受的最大长度是 `0x10000`。

## 本机证明

在 Darwin 上连续跑了两遍 `src/resource_network/test_closed_route.py`，退出码都是 0。`necko_quic_init` 返回 0，版本字符串是 `neqo-0.32.0/nss-3.131`。

- 明文是本机 HTTP/1.1。状态 200，版本码 11，正文与服务端标记一致，响应里没有 `804b0033`。
- HTTP/3 用本机 `neqo-server`。同一来源第一次 ALPN 为 `http/1.1`，版本码 11，状态 200。第二次状态 200，版本码 30，正文 64 字节与该服务器对路径的响应一致。
- 备用端点证书不受信任时，第二次失败，错误是证书校验失败，状态不是 200。
- 公网 `quic.nginx.org` 连续两遍：第一次状态 200、版本码 20，响应里的 `Alt-Svc` 被记住；第二次状态 200、版本码 30，正文 2523 字节，与第一次的摘要一致。`cloudflare.com` 和 `www.cloudflare.com` 的 `/cdn-cgi/trace` 两次都是状态 200、版本码 20，响应里没有 `Alt-Svc`，所以仍走 HTTP/2。
- WebTransport 打本机回声 `127.0.0.1:9443`。Begin 帧带上叶证书哈希。协商成功，会话状态 200，数据报 `ping` 原样返回。不带哈希且 PKI 打开时失败，错误是证书校验。证明结束后服务仍听在 `127.0.0.1:9443`。

编译在 Mac 上离线完成。`tools/build_necko_quic.py` 把 Neqo 依赖改写到 Firefox 树里已经有的 `nss-rs` 路径，不修改 donor 源码。`cargo --offline` 会在应用 git patch 之前尝试克隆 `nss-rs`，所以生成的工作区不使用那条 git patch。链接需要 donor 的 `libfreebl3.dylib`。`libclang` 用 Xcode 工具链里的那一份。`neqo-bin` 的 bench 开发依赖不在 Firefox vendor 里，只从 overlay 里的 `neqo-bin` 清单去掉。不要 `cargo update`。不要提交 `libnecko_quic.dylib`、`neqo-server` 或 `native/necko_quic/Cargo.lock`。

`NECKO_EXTRA_CA` 只在校验打开时追加一张 PEM，不替换 Firefox 根证书。未设置时仍是原来的失败关闭。它不是产品信任库的改动。
