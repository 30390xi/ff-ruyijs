# Resource / Network owner 与指定 Necko 接入

## Host-private Cookie export (2026-10-25)

- State owner: the existing Context StorageArea / domain CookieStore, shared by
  document.cookie, CookieStore, HTTP, frames and successor documents. Snapshot
  copies all unexpired records without URL/HttpOnly filtering or jar mutation.
- Adapter: model_storage CookieSnapshot translates the domain records at the
  existing exception boundary. runtime/cookie_control validates host requests
  and returns plain snapshots; rpc resolves the active document's Model each
  time so navigation retirement never selects an unbound environment/clock.
- Host contracts: Python export_cookies/cookie_header and C op:cookies share
  that native handler. Header selection reuses CookieJoined(http=true), including
  domain/path/Secure/expiry/SameSite and duplicate-name ordering. Caller supplies
  initiating site, top-level-navigation flag and method; no initiating site is
  the default. This helper mirrors current runtime policy, not a new cookie jar.
- Private surface: no page installer, property, function, lexical binding or
  document.cookie/CookieStore replacement. No page getter is invoked by export.
  Header strings and record strings preserve jar bytes through Latin-1; expiry
  and creation use Unix seconds, session expiry is null.
- Tests: tests/test_cookie_export.py exercises Python and C ABI, HttpOnly,
  metadata, distinct paths/domains, expiration/deletion, same-site rules, frames,
  navigation, isolated Contexts, byte preservation, input validation and page
  enumeration/getter isolation. These are host controls, not new browser APIs.
  tests/test_cookie_necko.py sends the exported HTTP header through the existing
  external NeckoSession transport to a local HTTP echo server. All 17 Python/C/
  external-transport cases pass against the rebuilt runtime.
  External Necko responses require caller-managed Set-Cookie handling; internal
  Necko requests already consume this Context's jar automatically.

## Fetch CORS response visibility (2026-10-23)

- Bounded context / state source: the existing Resource Network Request
  snapshot owns `mode` and `credentials`; `HttpExchange` compares the request
  origin with each response hop and records cross-origin taint in the existing
  `HttpResult` value. Raw headers remain owned by the transport result.
- Production adapter and factory: `native_fetch_lifetime.cpp` carries the
  snapshot through `NativeFetchRecord`; `worker_network_wire.h` carries the
  response taint bit; `response_object.cpp::ResponseObject` remains the one
  shared response-view factory and applies forbidden-header removal, the
  CORS-safelisted/exposed-name projection, response type selection, and the
  original Headers owner's immutable guard for fetched Response headers.
- Installer and interfaces: unchanged original Resource Network Request,
  Response, Headers and fetch installers in Window, iframe and Worker realms;
  no new interface or state owner.
- Semantics in this slice: cross-origin `mode: cors` returns type `cors` and
  only safelisted or explicitly exposed response headers (with `*` treated as
  a wildcard only when credentials mode is not `include`). Cross-origin
  `mode: no-cors` returns an opaque response; same-origin responses remain
  `basic`; manual redirects remain `opaqueredirect`. Set-Cookie and
  Set-Cookie2 stay forbidden in every response view.
- Validation: the local two-origin Firefox parity fixture covers Window,
  iframe and Worker, explicit/multiple/case-insensitive expose names, wildcard
  with omitted/included credentials, cookies, body/status/type, clone and
  receiver/prototype checks. The live Turnstile trace comparison records the
  previously excessive Worker-visible headers.
- Boundary: this is response-view filtering only. Full CORS allow-origin
  rejection, preflight, and all redirect checks remain separate existing
  Resource Network debt; do not describe this slice as complete CORS.

## Original AbortSignal lifetime correction owner record

- Owner/factory/installer: original ResourceNetwork AbortSignal/NewAbortSignal;
  existing Window/Worker/Request/TaskSignal/Writable consumers share it.
- State: original aborted/reason/handler/dependent/algorithm slots; one additional
  private native lifetime record contains no JS edges or signal state.
- Event target: existing Interaction AttachBorrowed/RefreshBorrowed/ReleaseBorrowed;
  no alternate event/listener owner or target registry.
- Lifetime: foreground finalizer unlinks/releases, original ResourceNetwork State
  destructor detaches live records before Interaction teardown, finalizers invoke
  no JS and allocate nothing.
- Evidence: abort-owner-gc-content-firefox.json plus W1 strict native GC before;
  original broad donor includes existing listener-cycle and any-source debt.
- Gates: rebuilt actual EXE/C DLL, W1 signal GC correction, functional/retention
  preservation, original Abort/Request/TaskSignal modules and B1/R2/R1 replay.


## WritableStream W1 owner record (candidate)

| Field | Record |
| --- | --- |
| Owner | Original ResourceNetwork Streams owner |
| Previous implementation | Worker-only writable classes/tables/factory in transform_stream.cpp, removed atomically |
| Installer | One InstallWritableStream through original Window/Worker ResourceNetwork assembly |
| Factories | Original State/WebKit/WebFace; original PageReference retains realm-local fallback prototypes |
| State | Stream status/error/writer/requests/backpressure; controller callbacks and queue; writer ready/closed |
| Signal | Existing NewAbortSignal and AbortSignalAbort/event owner, no independent signal state |
| Default signal error | Original AbortException optional message; old callers unchanged, writable donor text supplied |
| Lifetime | Traced slots and reactions; no permanent instance registry or raw State reaction capture |
| Promise realms | Stream operation requests; writer ready/closed and size-error rejection; method binding/released errors |
| Evidence | writable-w1-before/behavior/realms/surface/reentrant/gc-content-firefox.json |
| Extra evidence | writable-w1-review-content-firefox.json and writable-w1-retention-gc-content-firefox.json |
| Native before | SVG B EXE fb955cad4d722b88799c3b79ad721712156ea6734fe825858914d21f7a6483a8, actual DLL 9c4759ca177b3a42713119cc9e07d23b6f352db195752857e34c758daa997fe7; interfaces equal, 27 donor differences each |
| Gates | Build identities, strict Python/C parity, six major Window/iframe GCs, full R1/R2/B1/module preservation and replay |
| Boundaries | Forced native Worker GC; original realm callback scriptability on removal; Body stream input/tee/pipe/Transform separate |

Each new handwritten source file stays below 200 lines and belongs to one
algorithm boundary. Original flat product source discovery remains the only
compilation list. This addendum is artifact-only until root integrates the slice.


## Body S1: original operations prepared for native streaming (2026-10-21)

`BodyRecord` and its traced `BodyHolder` remain the sole body lifetime/bytes
owner; the existing stream slots/FIFO/default/byte controller remain the sole
read owner. `ReadDefaultReader(State&, reader, result)` is the original inner
read operation in `readable_reader.cpp`, called after the original public
brand check. It enters the reader realm and wraps its original Promise back
to the caller. It holds no asynchronous raw State reference.

`body_result.h` declares value-only conversion. Text/JSON/ArrayBuffer live in
`body_result.cpp`; Blob/FormData remain in their existing owners alongside
`PageConstruct`, `MimeEssence`, `AppendUrlencoded`, `AppendParts` and multipart
parsing. JSON decode failure still propagates native failure, while JSON parse
failure preserves the pending error as the rejection value. FormData's two
known parse failures return their original static label to the caller's
existing TypeError Promise path. Callback failures preserve their native
exception outcome. No helper manufactures a second Promise.

Public methods retain FunctionOwner/BodyInterface capture before ConsumeBody
writes the aliased callee/rval slot, then the existing Observe, acquisition,
MIME lookup and settled/rejected flow. The initial Body error messages and
exact Request/Response brand check remain in `body_consume.cpp`.

Evidence: `body-s1-candidate/buffered-before.json` records 110 Body calls and
6 factory/header-effect cases in each actual Python/C Window and Worker,
plus 20 Window borrowed-success realm cases. Probe hash and row shapes are
enforced by `preserve.py`; errors/boolean types/key presence/order are compared
strictly. `buffered-before-donor-debt.json` separately records all 209 donor
differences per interface, including Worker FormData absence. This is a
preservation slice, so those differences are not hidden or relabeled as donor
parity. `debt.md` groups their meanings.

No Response stream input, native alias variant, tee, Cache, Writable/Transform,
Request upload or new retire flag is installed here. Buffered success realm
and clone realm debt remain explicit; streaming conversion and lifecycle are
reserved for the atomic Body slice after default/byte tee donor evidence.


## Body binding errors discovered during B1 validation (2026-10-21)

Original ResourceNetwork Body.consume and Request/Response clone retain their
single BodyRecord, lock checks, factories and installation. The shared consume
helper now validates the native receiver against the callee's retained interface,
reports the original method name, and rejects invalid receivers in an intrinsic
method-realm Promise. Clone reports synchronous binding/consumed errors through
the same static format factory. No stream state or successful consumption path
is duplicated. The helper moves from body.cpp into body_consume.cpp (under 200
lines) to keep this one binding/error responsibility shared by all five methods;
formatting uses fallible engine error reporting, not allocating STL strings.

Evidence: body-error-messages-content-firefox.json records 48 shared Window/Worker
rows plus 120 iframe rows; body-errors-before-b1-fix.json preserves the native
before result. Native validation must match exact brands, errors, Promise realms,
locked-versus-used state and preserve R1/R2/B1. Successful streaming Response
construction/clone remains a separately designed original-Body slice.


## ResourceNetwork byte/BYOB B1 ownership (2026-10-21, before integration)

| Field | Record |
| --- | --- |
| Owner | Original ResourceNetwork Streams and Body owner |
| State | Shared stream status/error/current reader/disturbance; controller callbacks and flags; one byte queue and pull-into FIFO |
| Numeric storage | Finalized native uint64 records only; every JS reference remains in traced object slots |
| Factories | Original stream/default-reader setup extended; same realm factories create byte controller, BYOB reader/request and intrinsic views |
| Installer | Original InstallReadableStream only; existing flat source discovery |
| Buffered source | One BodyHolder/BodyRecord; original delivery job emits bytes once into byte queue, original keepalive task emits EOF; no second consumption cursor |
| Callbacks | Original captured-source conversion and intrinsic start/pull/cancel reaction path with byte dispatch |
| Realm | Successful BYOB Promise/result Object in reader realm; pending typed view/buffer in operation realm; queued/closed view/buffer in reader realm; validation rejection Promise/error in method realm; lazy request in controller realm and request.view in getter realm |
| Transfer | Native ArrayBuffer contents transfer and detach; shared/resizable rejected during WebIDL conversion; Wasm fails without detach, preserving input-buffer error realm and reader Promise realm |
| Lifetime | Traced byte queue, descriptors, requests and callback reactions; no persistent instance registry or State reaction capture |
| Evidence | Independent buffers/lifecycle/realms/transfer-realms/surface/Body/review probes in actual content/Worker; Firefox six actual major GC/CC cycles |
| Gates | Native build, strict interface/donor parity, all R1/R2 preservation, module regression, root replay |
| Deferred | Native forced Worker GC (separate process, no collect control); tee/from/transferable streams, complete pipe/Writable/Transform, streaming HTTP transport |

Every new handwritten source file is split by one responsibility and remains
below 200 lines. This proposed addendum precedes candidate code; root integrates
it with original architecture/owner documents before any production application.
Paired pull-into/read FIFO edges publish only after allocation and native source
scheduling succeed; pending read Promises remain linked through result settlement.
Body's native EOF suppresses only its expected leftover-element terminal error
after all stream rejection steps succeed. OOM/settlement failures still propagate,
and failed native source emission errors the stream instead of becoming a normal EOF.


## Default ReadableStream sources R2 (2026-10-21, before implementation)

| Ownership field | Proposed record |
| --- | --- |
| Bounded context | Existing ResourceNetwork Streams owner, no new runtime owner |
| State source | One StreamStatus/storedError/source kind; existing reader lock and pending-read queue; default controller slots own captured source/callbacks, HWM, queue total and chunk FIFO |
| Disturbance | Existing BodyRecord.used for buffered Body; SDisturbed for general sources; one DisturbReadable helper |
| Factory | Existing NewReadableStream/NewDefaultReader; one internal default controller factory using current State.defaultController prototype |
| Installer | Existing InstallReadableStream calls InstallDefaultController; no alternate global or conditional installer |
| State lifetime | Native objects with traced reserved slots; chunk/request linked lists; intrinsic reactions retain controller only; original R1 BodyHolder finalizes BodyRecord |
| Source algorithms | Source dictionary converted once; start synchronous; start/reading SafeResolve; pull/cancel original Promise.resolve in callback object realm; serialized pulling/pullAgain with no polling |
| Terminal transitions | Shared CloseReadable/ErrorReadable/CancelReadable; close drains queued chunks; errors preserve exact reason; cancel drops chunks and adopts a separate Promise fulfilled with undefined; release preserves source queue |
| Realm | Reader promises and queued default chunk results in reader realm; public enqueue/close pending results and validation errors in method callee realm; only private queue storage enters controller realm; cancel return follows source callback Promise realm |
| Buffered integration | Existing delivery job and EOF keepalive port; original Body consume/clone lock and disturbance; Closed bit migrated atomically to status in holder/delivery/completion |
| Interface parity | Same Window/iframe/successor/Worker owner and Python/actual C DLL assembly |
| Evidence | readable-default-firefox.json: 17 Window/Worker rows; readable-r2-timing-firefox.json: six shared rows, eight callback realm and 24 result realm combinations; surface probe: ten shared/four Window-only groups; cancel probe: six shared rows; major-GC fixture: six explicit collections and detached iframe retention/release |
| Pending validation | Candidate static review only; native compile, C/Python/Firefox strict parity, descriptors/receivers, true major GC, detached iframe/Worker, R1/module regression and coordinated site replay |
| Deferred | Byte/BYOB, tee/from/transfer, stream Body input, complete pipeTo, Writable/Transform algorithms |

File responsibilities: readable_controller defines interface/slots; readable_source
converts WebIDL dictionaries and invokes captured callbacks; readable_pull owns
intrinsic reactions and pull serialization; readable_queue owns chunk queue and
controller operations; readable_status owns shared errors/cancel/disturbance.
Existing factory/reader/members/delivery/completion/holder files are extended in
place. Every candidate handwritten source file remains below 200 lines. The
original flat product_sources discovery picks up new cpp files without another
build source list.

This is a proposed owner record for integration review. Production owner and
ARCHITECTURE documentation must receive these addenda before production application;
the artifact itself makes no build or acceptance claim.

Actual-content Firefox revalidation uses window.eval plus in-page JSON serialization. Source, timing, cancellation, surface and six-major-GC results match the preserved original captures; corrected capture artifacts end in -content-firefox.json.


## Buffered Body/default reader R1 (2026-10-21, before implementation)

| Ownership field | Record |
| --- | --- |
| Bounded context | Existing ResourceNetwork; no alternate transport, JS shim, or language-specific API layer |
| State source | Existing BodyRecord bytes/nullBody/kind/used; used is the sole disturbed state |
| Lifetime | New internal BodyHolder uniquely finalizes BodyRecord; Request/Response slot0 and stream reference it; holder caches stream; no strong Response back-edge |
| Factory | Existing ResponseNew/RequestNew commit the raw record into an initially empty holder only after all fallible work; their callers retain unique_ptr ownership on failure |
| Stream state | Native reserved slots hold cursor, terminal state, exclusive reader or internal-consumer marker, and a traced linked list of pending read promises |
| Reader state | One stream reference and one stable closed Promise; release rejects outstanding reads, preserves undelivered bytes, and updates the same stream lock |
| Production installer | Existing InstallReadableStream replaces its old facade atomically and adds the DefaultReader interface; same Window/iframe/Worker entry and retained realm factories |
| Promise/realm | Dynamic intrinsic Promise jobs capture traced stream objects, never raw State pointers; byte arrays and queued-read results use stream realm, reader promises use reader realm; already-closed read results use reader realm |
| EOF completion | Existing keepalive task port runs native EOF after chunk reactions, including detached iframe streams; Worker assembly binds/clears the same scheduler port; callback captures only traced stream |
| Body integration | All five consume methods and clone share lock/disturbance checks; internal consumption drains and permanently locks the exposed Body stream; Request transfer uses the same disturbance transition |
| Interface parity | Independent Firefox Window/Worker/iframe baseline; rebuilt actual C DLL identity and Python/C scripts, GC and constructor overwrite coverage |
| Remaining debt | Arbitrary underlying source callbacks, BYOB, tee, transfer and real pipeTo are separate slices; existing pipeTo entry is preserved |

Responsibility split: body_holder.cpp owns only holder allocation/lookup/finalization
and Body integration; readable_factory.cpp owns interface factories/install;
readable_members.cpp owns stream members; readable_reader.cpp owns reader members;
readable_delivery.cpp owns intrinsic jobs and read queues; readable_completion.cpp
owns the single native EOF task. Each new implementation
file stays below 200 lines; tools/product_sources.py already discovers flat owner
cpp files, so there is no second source registration list.

Independent evidence: artifacts/zhipin-20261021/readable-firefox.json contains 17
identical Window/Worker cases, child-realm results, and an explicitly inconclusive
allocation-pressure-only WeakRef check. Donor Fetch.cpp:1947-1974 and
UnderlyingSourceCallbackHelpers.h:298 create an input-backed native stream without
a Response back-edge. The original raw-pointer lifetime hazard is a static finding;
it is not reported as an observed production crash.

## ByteLength / Count 队列策略（2026-10-21，实施前记录）

当前生产路径没有 ByteLengthQueuingStrategy 或 CountQueuingStrategy 定义。两者属于原
ResourceNetwork Streams owner，本片在既有原生接口机制中实现完整策略值对象。
donor `dom/webidl/QueuingStrategy.webidl` 声明 required unrestricted double；对应
ByteLengthQueuingStrategy.cpp / CountQueuingStrategy.cpp 确认 HWM 不做非负检查，
size 函数分别执行 GetV(chunk, byteLength) 与返回 1。

| 项 | 唯一 owner / 本片通路 |
| --- | --- |
| 有界上下文与状态源 | ResourceNetwork；实例原生 slots 中的构造参数 HWM 和 realm size 引用，没有队列、网络或流状态副本 |
| 工厂 | 原 State 的 WebFace / WebKit 创建两种独立品牌实例与原型；constructor 先读 newTarget.prototype，再做 required 字典及 ToNumber 转换，保留 NaN、±Infinity、-0 |
| 原型 fallback | newTarget 的 prototype 非对象时按 GetFunctionRealm 查其原 factory prototype；原 runtime PageReference 增一槽 tracing ResourceNetwork 创建的原型对，该对象仅保留工厂根，不是第二状态源或公开 API |
| 成员 | readonly highWaterMark 返回原 slot；size getter 验证精确 native class，返回实例构造 realm 的共享非构造函数；byteLength 的属性读取和异常沿原 JS 引擎 |
| 安装与暴露 | 唯一 InstallQueuingStrategies 在原 InstallSurface / InstallWorkerStreams 的 ReadableStream 安装前执行；Window、iframe、successor、Worker 同实现 |
| 生命周期 | 原 State 根保持两种 interface 和 size 函数；实例仅持数字及 GC-traced JS 引用，无 native heap record；跨 realm getter checked unwrap 后 wrap 返回 |
| 验证 | 独立 Firefox 对照 constructor/descriptor、字典转换顺序及异常、值域、size 品牌和函数身份、newTarget/跨 realm fallback/公开覆盖、Worker；Python 与实际 C DLL 使用相同 JS、真实 GC 和原生产入口 |

新生产 `queuing_strategy.cpp` 单一职责、低于 200 行。本片不把策略存在性当作其它 Streams
能力完成；Writable/Readable/Transform 的完整队列算法仍按原 owner 后续独立推进。

## data: URL 的 scheme fetch（2026-10-20）

owner仍为ResourceNetwork原HttpExchange::Prepare（PerformHttp唯一路径），fetch、
XHR、img、样式等调用方不变，无新installer或状态。纯解析在domain层
`domain/resource_network/data_url.h`（WHATWG data: URL processor：百分号解码、
`;base64` forgiving-base64、MIME序列化，默认`text/plain;charset=US-ASCII`），
无V8/Necko依赖。data:在本地作答200/OK与content-type，不走代理、不附Cookie；
NetLog仍记录一条状态200。解析失败为网络错误。原先data:被当作代理目标报
`Unsupported proxy scheme: data`，browserscan.net的data: SVG图标因此失败。
验证：artifacts/browserscan/data_url.py 与独立Firefox（`--firefox`）9项对照一致，
仅失败文本不同——Firefox统一为`NetworkError when attempting to fetch resource.`，
ruyijs沿用既有的原始错误文本透传（所有fetch失败的既有差异，单独处理）。
canvas.fp回放 same 1379 differ 0 不变。

## Worker 取消信号装配（2026-10-11）

状态源仍为原 AbortSignal slots；唯一工厂 NewAbortSignal/AbortSignalAbort，唯一安装器
InstallAbort。Realm::InstallCancellation 仅委托该安装器，供 Worker 在原 Interaction/
DOMException 后装配，不创建目录、Headers arena、Storage。主页面 InstallSurface 顺序
保持；每个 Worker 的 Resource Realm 生命周期覆盖脚本与消息循环，析构空状态安全。
实际 C DLL/Python 共用 RunWorker；worker_abort_firefox 独立合同验证品牌、描述符、
非法接收者、signal/异常身份、可信事件与 public constructor 覆写。timeout 的原延迟
端口在后续 Scheduling 片绑定，禁止另建 timer 状态或复制 Abort 实现。

## Request 转换和元数据（2026-10-11，实施前）

本片已发布：54项4子项聚焦与58项46子项网络回归通过，独立Firefox 88/90构造
场景一致；两项Unicode URL编码仍归原URL owner后续修复。默认4项2子项及
旧部署回放通过，实际DLL/源码身份见request-conversion-validation/publication.json。

owner仍为ResourceNetwork原RequestInput/RequestDraft/Request实例slots；数据来源
是输入值、原Request记录及原HttpContext环境source。request_init只做瞬时WebIDL
字典快照，request_validation只做原DTO校验，均不保存Context状态。原RequestNew/
InstallRequestObject负责destination及字段，C/Python/iframe共用原有装配顺序。
独立Firefox覆盖字段顺序、转换异常、枚举/组合约束、referrer、URL、缺参/length、
descriptor/品牌；原生fetch实际收据证明同一输入路径，GC与子realm证明原工厂。
Headers guards、signal身份/跟随、完整BodyInit、baseURI仍是明确后续工作。

## Body 存在性与 Firefox 接口（2026-10-11，实施前）

已完成本片：60项2子项聚焦与58项46子项网络回归通过，独立Firefox/实际C DLL/
Python合同一致，旧部署回放通过；冻结源码构建核验后默认产物成套发布。
范围与剩余Streams债务详见tasks/BODY_PRESENCE_20261011.md。

- owner/state：原 BodyRecord.nullBody 保存正文是否存在，bytes 保存正文内容；
  RequestDraft 增加瞬时存在性值。无正文的重复消费不改变 used，零字节正文会消费。
- factory：BodyInput、RequestNew、ResponseNew、ResponseObject 保持唯一；clone
  携带同一存在性状态，Request 转移延迟至校验成功，显式替换可来自已消费 source。
- installer：原 InstallBody 只给 Response 安装 body；Firefox Request 没有该属性，
  donor webidl 与原生独立探针一致。其他 Body 方法与原有安装顺序保持。
- source：构造参数/原 Request 记录/原 PerformHttp 结果与 HEAD 方法，禁止按字节
  长度推断正文不存在。Firefox HTTP 204/205/304 仍有空流，构造器则拒绝显式空正文。
- parity/tests：C/Python 共用生产装配；独立 Firefox JSON、真实本地 HTTP 收据、
  receiver/descriptor/child realm/GC/原工厂与未插桩旧部署回放。旧 default_fields
  null 预期按独立 Firefox 修为 undefined，原基线失败保留，不作为原断言修复通过。
  同时原 Response('x').body 的 null 占位预期依独立 Firefox 改为有正文的存在性断言。
- 边界：既有 Response 流 facade 不扩充；完整 Streams 品牌、锁定、身份、reader
  寿命及跨 realm 借用属于后续 owner 迁移，不据此片宣称流 API 已完整。

## 默认请求头与Referrer（2026-10-11，实施前）

发布补记：43条Firefox请求回显一致，Python/实际DLL的7项7子项通过；扩展
91项40子项通过，原Request.body空正文测试在旧/新产物均失败并保留。构建
输入2305、冻结文件4958均核验；缓存传输13项15子项、默认安装2项2子项通过。
唯一网络路径已发布，Google仍为自然SG_SS发送后的429，不计完整实网闭环。

原owner为Model/PerformHttp/RequestDraft；factory为原Request/Response工厂，安装
顺序仍InstallSurface。状态仅当前请求DTO；语言来自原Environment Describe，
地址来自原URL owner。新纯策略不创建状态，所有调用方仍共用PerformHttp，C/Python
同路、iframe沿原realm source。基线回显证明缺Accept/语言/Referer/Fetch Metadata；
独立Firefox也证明脚本自动click没有Sec-Fetch-User。保留已有用户头，逐跳计算
默认头和Referrer-Policy。完整Request转换/CORS/文档策略等独立债务明确保留。
测试覆盖真实Firefox完整回显、实际C/Python、覆盖/GC/跨源/降级/redirect；旧网络
和Performance回归及实网搜索作为最终门禁。无第二installer或Header/Cookie状态。

唯一默认头迁移：删除image_load_ports的固定UA/语言/站点/Referer与Beacon的
独立Referer计算；入队的Beacon保存原文档地址，iframe在提交目标地址前取得
原HTTP环境。图像/Beacon/iframe仍借原端口，没有新API或状态owner。重定向
沿已裁剪的referrer继续计算，后续宽松策略不会恢复已移除的路径或来源。
样式原端口也移除重复Accept/Referer，保留原CORS Origin。独立Firefox edges
回显与ReferrerInfo.cpp确认默认ETP把跨站子资源的三种宽松策略收紧为默认策略；
按该默认执行，顶层导航例外。自定义ETP偏好、站点例外与私密模式仍待验收。

## HTTP Cookie 与环境头（2026-10-11，实施前）

最终阶段验收：cookie-network-validation/final全量构建，2294个输入SHA一致，
50项36子项通过，原010341结果页回归通过；默认shell/DLL/helper成套发布。
SG_SS已由原页面自然生成并在原NetLog中记录为出站cookie名；服务端429，实网
结果仍未闭环。完整CORS/第三方分区、站点配额等其余Cookie合同仍待后续验收。

实网补记：原生Fetch证明Google首页主脚本触发原传输1MiB上限。
同一ResourceNetwork片将necko_wire和oneshot的边界对齐原Limits::body_bytes=8MiB，
无新增状态/HTTP实现；原Necko测试按8MiB复验完整边界与超限/截断/Job回收。

唯一 owner：StorageArea 组合的 domain/resource_network::CookieStore，替换旧 cookie
map。唯一生产入口：Document.prototype accessor 与 PerformHttp 每跳；原 Model
通过端口借用该 store，无独立状态。URL 来自原 BrowserEnvironment/Document，
UA 来自原 Environment::Describe，时钟来自原 Scheduling。工厂/installer 仍为
InstallStorage，安装顺序不变；main/iframe 由 runtime 同一端口装配，C/Python共路。
Fetch credentials/XHR.withCredentials 控制跨源发送与接收；不在 Python/helper 建 jar。
验收：独立 Firefox cookie 合同、本地 HTTP Set-Cookie/重定向/回显、作用域/过期/
HttpOnly、原型/receiver/跨realm、GC/覆写、实际 DLL、既有网络与旧页面回归。
出站观测只在原 NetLog 中保存 cookie 名，保持原字节预算；真实搜索单独验收。

## 显式代理（2026-10-11，实施前记录）

Python Context(proxy=...) 为本次shell建立独立启动环境，原Necko子进程继承
RUYIJS_HTTP_PROXY。原necko_oneshot.py把它交给唯一HTTPAdapter/open_response，
None或空串直连。ResourceBundle/NetLog、请求生命周期、TLS验证、响应配额及
页面surface保持；不把代理配置暴露为页面属性，不建立第二网络或cookie状态。
Python/C仍使用同一helper；原始C ABI通过启动环境配置。验证见
tests/test_network_proxy.py及原网络测试，实网Google首页单独记录。搜索全流程待办。

## HTTP 重定向（2026-10-11，实施前记录）

- 边界：仅 ResourceNetwork。此前独立 Firefox 302 探针见
  artifacts/navigator-platform-validation/redirect-check/report.json。
- owner/state：RequestDraft 是请求参数源；PerformHttp 是唯一生产请求协调路径；
  Model/ResourceBundle/NetLog 是唯一资源/日志源。领域纯函数负责状态码、方法和头规则，
  adapter 复用原 UrlValue 解析 Location。调用期状态不跨任务或 Context 保留。
- factory/installer：原 ResponseNew/ResponseObject、XHR 工厂与 InstallSurface 不变；
  fetch 将 Request.redirect 传至该路径，XHR/脚本按 follow 消费同一 HttpResult。
  manual 过滤结果的无体标记归原 BodyRecord；不复制 Body 或 Streams 安装器。
  XHR.responseURL 投影真实响应最终地址；尚无响应及 reopen 后为空，已用真实Firefox取样确认。
  公开iv8提前暴露请求地址的fixture保留，比较测试分别断言这一已证明的差异。
- 寿命/预算：最大跳数和单链期限有界，Necko 每跳仍通过原 Job/pipe 关闭；所有跳转共用
  原20秒总期限。短命请求体仅在变更方法时清空；不为每跳累计保存响应正文。
- 日志：原日志 ID 对应每跳，保留 matched 表示资源目录命中的含义；真实响应状态独立更新。
  不接入未验收 LiveRequestTable，也不建立第二日志或长期请求表。
  日志满时保留原有离线/在线请求行为，观察容量上限不成为请求拒绝条件；4096条基线单独验证。
- 边界：cookie jar/CORS 完整策略、退出 keepalive、导航历史和样式表最终基址尚需所属
  owner 的独立验收；本记录不将它们宣称为已完成。
- 验证：Firefox 探针覆盖 follow/manual/error、方法和正文、相对地址/fragment、错误及
  跨源头；生产测试补充混合资源链、循环上限、子 realm/GC/覆写、C DLL/Python 同路、
  既有网络行为、完整 Go 及182516。新增文件按纯规则、请求协调和测试职责拆分，各<200行。

## 210110 Response body stream（实施前记录）

Response/Body 仍由本 owner 的 `BodyRecord` 保存正文和消费状态；`Response` 工厂、fetch、clone、
Headers、调度和每 realm 安装路径不变。`body` 只创建同一记录上的 ReadableStream reader 适配，
reader 返回原正文的字节并通过原消费门禁更新 `bodyUsed`，不建立第二资源目录或响应缓存。
真实 Firefox 参考覆盖 body/getReader/read、done/value、clone 后双读、非法 receiver、跨 realm 和 GC；
案例回放只验证业务脚本选取的 fetch 分支。实际 C DLL/Python、原 Body/Response、Go 与 191206
回归是阶段门禁；Streams 其余接口仍按同一 owner 单独补齐。

## 第八十七片：XHR LOADING 文本投影（实施前记录）

- 证据：Firefox 157 经 D:/ruyipage 运行 `xhr_firefox_loading.js`，原生 HTTP
  20261009T091924Z 确认 default/text 的 response 在 readyState=3 等于 responseText；
  arraybuffer 保持 null 直到 DONE。旧产品 7 项 XHR 聚焦基线通过，但同探针四处
  LOADING response 为空，保存于 `artifacts/xhr-loading/before.json`。
- bounded context/state source：仅 ResourceNetwork；原 XhrRecord.state 和
  HttpResult.body 保持唯一来源，只修原 XhrResponse 文本读取门槛，不建响应副本。
- object factory/installer：原 xhr_object、xhr_response、InstallXhr 和
  Realm::InstallSurface → InstallPage 顺序不变；主/子 realm 和跨 realm 品牌沿原路径。
- interface parity：C/Python 共用原 runtime；重建实际 js.exe/ruyijs_client.dll，
  复测聚焦 XHR、同一 probe 的原始 C ABI 与 Python 完整结果。
- tests：Firefox 完整 JSON 对照，状态 1/2/3/4、default/text/arraybuffer、
  原型/descriptor/非法 receiver/跨 realm、GC 与覆写、Necko 大响应及分段输入。
  public iv8 的 LOADING 空 response 留作明确差异，不修改公开版证据。
- 本片不声称实时流式交付：现有 PerformHttp 仍在完整收取后发布 2/3/4；
  多次 LOADING/progress、timeout/abort 的在途取消继续属于原传输待办。
  案例 HTTP 补环境桥的错误响应/204 规则另外标记为案例修正，不计生产原生能力。
  按用户验收范围不做 Go、性能或全领域回归。所有新增手写文件小于 200 行。

## 第六十九片：Necko大响应管道阻塞（实施前记录）

- 唯一语义owner仍为原Model/ResourceBundle/NetLog；PerformHttp先Record并查
  原目录。NeckoHttp是既有oneshot的传输适配，不增加第二状态或公开API。
- 来源和基线：锁定obj-necko wheel-v18；片前网络15项全部通过。当前实际DLL
  的native XHR 1024字节HTTP200，16384字节20秒status0，证据在
  artifacts/necko-transport/before.json、baseline.log。先等进程退出再读响应
  是明确的反压死锁；不以增加管道容量作为修复。
- 对象工厂/安装顺序：原InstallXhr和fetch/Request/Response/Headers以及主/子
  realm工厂、Scheduling端口、runtime coordinator不变；C/Python使用同一通道。
- 传输寿命：每次请求独立worker和两条内部命名管道；父端overlapped读写可取消，
  所有读写和进程等待共用20秒总期限。父进程在worker运行时读取stdout，不占
  额外工作线程，不触碰JS。继承列表仅本次stdin/stdout/NUL，不继承其它Context
  或请求句柄；失败完成取消的I/O后终止本次worker，最多5秒回收，再关闭句柄。
  补充实际证据：首版客户端3秒超时后js.exe退出，Necko PID38704仍活着，
  直到fixture释放响应才退出（client-timeout-first.json）。worker必须暂停创建，
  加入本请求KILL_ON_JOB_CLOSE Job后才恢复运行；Job句柄不继承，Context被
  强制结束也自动清理worker。未增加跨Context全局Job或管理器。
- 协议/内存：原u32 little-endian字段顺序不变。body维持1MiB，头最多256项且
  累计64KiB，字符串64KiB，错误消息最多1024 Unicode字符。临时HttpResult
  验证状态/所有头/完整正文后再发布，不把半截数据作为成功。helper不建Session
  cookie jar，HTTP/NSS仍使用已有HTTPAdapter/open_response；正文按解码后计费。
- 拆分：necko_pipe只管Windows句柄与有期限I/O；necko_http只管worker启动和
  回收；necko_wire只管原DTO协议和边界。原page_http去掉被取代的进程/管道代码，
  每个新增手写文件小于200行；不引用src/transport的未接入候选实现。
- 测试：真实loopback的大文本/中文/NUL/二进制、fetch/XHR、跨realm与覆写、
  完整1MiB/超限/压缩解码后超限、大请求头/正文、错误后继续、退出无遗留worker；
  原资源优先和原网络15项基线；真实DLL/C API/Python同路及同部署原案回放。
  按用户范围继续不做Go、性能比较或全领域回归。本片不是全异步/Streams/
  cookie/CORS/redirect/多连接复用的交付，相关原待办保留。

## Scheduling整域的Abort依赖（实施前记录）

唯一InstallAbort/InstallAbortSignal保留；原signal的aborted/reason槽扩owner、onabort
和原生依赖列表，controller仍只持同一个signal。checked unwrap接受同源真实对象。
组合any从原工厂创建signal，原AbortSignalAbort遍历依赖传播同一reason；信号引用由
traced slots保活，没有第二取消状态。timeout调用coordinator提供的原Scheduling
延迟任务端口，使用原DOMException工厂引用创建TimeoutError，无网络独立timer。
onabort复用原Interaction监听派发；所有全局、静态、原型描述符按真实Firefox采样。
原signal槽另保存内部abort算法回调，先运行算法再派发用户abort事件。Scheduling通过
本owner的Add/RemoveAbortAlgorithm适配入口注册取消动作，用户stopImmediatePropagation
不影响任务取消；不以可覆写的JS监听器充当内部取消算法。
TaskSignal后续使用本native工厂/类，继承成员不单独重装。C/Python同一资源装配。
现有网络测试保持，额外验证any/timeout/品牌/proxy/跨realm、覆写和原请求闭环。
文件按signal对象/成员/组合/安装拆分，各新文件<200行；不扩网络传输语义。

## 2026-10-08 XHR 公开接口与事件生命周期（实施前记录）

实际请求案例通过后继续原 ResourceNetwork；本片只修改 XHR 这一网络接口族。
现有唯一生产 adapter/installer 是 `page_xhr.cpp::InstallXhr`，由
`Realm::InstallSurface → InstallPage` 在 Interaction、Scheduling 后安装；
旧实现公开 `_headers/_method/_url`、自有 writable 状态、同步直调 onload。
该定义将原子替换，不保留条件 fallback 或第二 JS 浏览器层。

- 状态来源：原 Model/ResourceBundle/PerformHttp 是资源与请求日志的唯一 owner；
  XHR 每个请求的状态流转值归本领域 `domain/resource_network/xml_http_request.h`。
  adapter 仅持此值、HTTP 字节结果、JS handler/upload 引用和事件目标借用记录。
  不引入第二资源目录、transport、计时器队列或 listener 容器。
- 工厂与寿命：每 realm 自有 XHR/EventTarget/Upload 原型；实例带品牌 slots，
  upload 同 realm 工厂且引用稳定。弱借用 Interaction 目标，由 trace/finalizer
  更新/释放；State 链表只管理 teardown 解绑定，不 root 实例。JS 引用由 slots
  trace；已排队任务保活接收对象和请求代次，过期异步任务停止投递。
- 依赖与顺序：ResourceNetwork 复用 Interaction 的原生 add/remove 和 trusted
  Event 派发；runtime coordinator 注入 Scheduling 的现有 PostTask 端口，
  子 realm 复制端口但用自身对象工厂。端口在 owner 释放前撤销，C/Python 共用装配。
  DOMException 使用既有 realm 工厂引用，无新异常构造器。
- 公开证据：`tests/probes/xhr_{contract,timeline,edges,responses,reentrancy,async}.js`
  由 public iv8 0.1.4 实跑；`tests/fixtures/*_public_iv8.json` 保存源码 SHA。
  默认 async=true，显式 undefined=false；response JSON/ArrayBuffer 每读重新产生，
  blob/document 为 null；同步重入和异步代次取消分别按实测处理。
  Firefox 三 WebIDL 均暴露 Window；本片不添加 Chromium 私有成员。
  后续 realms/upload 探针记录了 public 自有成员拒绝异 realm 借用的差异；
  用户明确要求优化 iv8 不良限制，以真实 Firefox 为准，因此本产品自有成员和
  base handler 均接受同源跨 realm 的原生接收对象。20261008T104901Z 通过
  D:/ruyipage 在真实 Firefox 157 双向 open/send/getter 实测成功，普通对象/Proxy
  仍 TypeError；源码 hash 与结果位于 artifacts/xhr-firefox。此启动使用独立
  profile 和 MOZ_DISABLE_CONTENT_SANDBOX=1 解决先前 SpawnTarget 失败，没有
  覆写页面 API，浏览器二进制保持只读。upload 生命周期继续走同一事件链。
  子 realm 本身仍使用既有共享资源目录（public 子 realm 未取主目录而返回模拟数据，
  该生产共享能力保留并独立测试，不将模拟正文或动态时间戳写入产品）。
  异步状态 2/3 后经 coordinator 注入的原 checkpoint 推进微任务，仍使用同一队列。
  XHR 自有调用的数据/URL基准始终取接收对象 owner；允许借用不意味着克隆主 realm
  原型。同源 iframe URL 使用既有 child 环境；跨源访问仍由引擎包装器校验。
  public 的 I/O 与 timer 优先级和本产品既有 PostTask 不同；对照断言状态/微任务
  的因果顺序，不把独立 timer 与 I/O 的竞争顺序记为浏览器保证。
  Firefox 实测同步 XHR 的 responseType 限制/事件压缩另保留为差异；本片保留
  public 已可用的宽松响应模式，不以增加补环境失败作为优化目标。
  Firefox 157 的另一份实测确认 JSON/ArrayBuffer 响应稳定；两种首次读取顺序
  进一步表明：首次 getter 的调用 realm 创建响应，后续读取共享该对象。
  child 首读时主 getter 得到 child 对象；主 getter 首读则 child 得到主对象。
  按用户新规则修正 public 每读新建的缺陷：原 XHR wrapper 新增 traced cache
  slot；首次方法 realm 工厂产物经 CCW 包入 receiver realm 保存，后续包回
  调用方。open/abort/new response 清缓存，无新增 owner/全局表。
  实测中发现已有 Necko oneshot 把非空 header pair list 直接交给 requests，
  导致请求尚未发出即报错。此原 transport 的 helper 原子迁入本 owner 源码
  necko_oneshot.py，build 唯一部署至既有 vendor 路径，源/部署 SHA 入产物记录。
  header pairs 转 CaseInsensitiveDict 并合并同名值，HTTP/资源/事件工厂不变。
  保留先前无头 UTF-8 baseline，并用真实 loopback POST+自定义头验证修复。
- 拆分：page_xhr 只负责安装；xhr_object 管工厂/品牌/寿命；xhr_members 管状态属性；
  xhr_handlers 管 handler 与原监听链；xhr_request 管 open/send/abort；xhr_delivery
  管任务与 HTTP 交付；xhr_headers 管头字段；xhr_response 管响应投影。
  新文件各低于 200 行，避免原单文件继续膨胀。领域状态不依赖 JSAPI 或 ABI。
- 基线：`artifacts/xhr-before/` 保存旧实现/关联测试；真实案例 120 对本地请求
  和响应、240 次 loopback、40 项 UTF-8 聚焦测试及两端远端首页/各两业务 200。
  实施后验证 descriptor/品牌/子类/iframe/覆写/GC/原日志、same-JS 公开探针、
  C/Python 同路与未插桩原案例。按用户范围不做 Go、性能和全领域回归。
  旧产物聚焦基线另实跑 34 项全部通过：page_network、iframe_realm 与两份
  network_body_text 测试，日志 `artifacts/xhr-before/focused.log`。
  原两个立即读取 XHR 的测试显式指定同步 false；默认异步由公开同脚本与新
  C/Python 测试断言。接口变化是公开契约修复，原同步资源正文能力继续保留。

本片保留现有同步 HTTP transport 的基础设施实现；异步 XHR 的 transport 和
完成投递通过原任务队列推进。实时网络超时中止仍需独立 transport 证据，
不因本片离线/loopback 生命周期测试通过而宣称完整浏览器网络已完成。

## 2026-10-08 中途实案：网络正文 UTF-8 解码

实施前 owner 记录：`envtrace-desktop-20261008-170959` 的 20 次原始脚本请求已在
iv8/ruyijs 输出一致；加入 10 份完整中文业务响应后，iv8 正常，ruyijs 的
`黄浦区` 成为 `é»…`。证据 `artifacts/midpoint-envtrace-20261008-170959/`
下 `result-*-p1-s42-fixtures.json`。`JS_NewStringCopyN` 将 HTTP UTF-8 字节按
Latin-1 解释；原 `Body.text/json` 也沿同一错误转换。

- bounded context / state：原 ResourceNetwork 的 Model/ResourceBundle 和
  HttpResult/BodyRecord 字节，不新增状态或副本；修改只发生在输出 JS 文本处。
- object factory / adapter：原 `page_xhr.cpp::Send` 与 `body.cpp::TextMethod/JsonMethod`，
  共用本 owner 无状态 `DecodeBodyText`，复用 SpiderMonkey 的 lossy UTF-8 解码，
  去一个起始 UTF-8 BOM，错误序列采用 U+FFFD；不经可被用户覆写的 TextDecoder。
  字符串 BodyInput/发送正文相应使用共享 EncodeBodyText 写 UTF-8；头字段仍沿
  原 ByteString。这样保留 `new Response('é').text()` 的已有效行为，并支持中文
  及内嵌 NUL，避免只改读端后把原 Latin-1 输入误当 UTF-8。
- installer / lifetime：原 `Realm::InstallSurface → InstallPage → InstallXhr`
  及 `InstallBody` 不变；主/iframe 各自原工厂，没有新 constructor/prototype。
- interface parity：Python/C 共用 runtime 和生产读取路径，重建 shell/实际 DLL；
  本次按用户限定仅做聚焦验收，Go、性能及全量领域回归仍延后。
- tests：中文/emoji/NUL/BOM/坏 UTF-8、Request/Response 消费规则、iframe 自有
  XHR 构造器与同一资源日志、receiver/描述符原路径；10 份真实响应与同部署
  未插桩 20 次请求重放。单个新增 helper 和测试文件各小于 200 行。

本案还观察到原 XHR 缺 EventTarget 继承、readystatechange 时序；本次使用原生
onload 验证最终状态及业务值，不将这两个差项记为已经对齐。其它字符集选择仍是
  原网络 owner 的独立待办，本片验收 UTF-8。

状态：2026-10-03。资源目录和 native Headers 已通过 `resource-headers-provenance`：403 Python/C、268 subtests、62 Go、资源领域33项；202/381来源目的通过，完整目标继续。Necko、fetch/XHR、cookie 与 worker 按依赖顺序实现；本检查点不代表页面网络已接入。

## 唯一 owner 与来源

用户指定网络实现使用 `D:\firefox\src\firefox\obj-necko`。选定可复核版本目录为 `nss-v20`，对应独立来源 `source-v20`、SDK `nss-sdk-v20` 与 `wheel-v20`（载荷相同、按 cp314 重新打包为 `necko_standalone-0.8.1-cp314-cp314-win_amd64.whl`）。2026-10-19 前为 `v18`（0.8.0 / Firefox 156 / NSS 3.128）。原目录只读；新产品复制、构建、缓存与测试写入 `ruyijs/`，不修改 Firefox browser 或用户 profile。

原项目领域 owner 是 `domain/resource_network/ResourceBundleAggregate`、`Resource`、`NetLog` 与 HTTP 值对象；原生产入口在 `infrastructure/network_impl`。离线 bundle、请求身份、请求日志、状态转换和资源匹配继续归这个有界上下文。SpiderMonkey adapter 创建 Request/Response/Headers、fetch、XHR 等 realm 对象，调用应用用例与 transport port，不实现第二份网络状态。

首轮目标是领域 owner 接入真实共享资源操作，并安装完整可独立使用的 native Headers。`ResourceBundleAggregate` 仍是离线资源和请求日志的唯一入口；`NetLog::record` 的条目 ID 和按 ID 更新能力保留。`HttpHeaders` 从并发导入草案的 map<vector> 收敛为单一有序列表，保留 append/set/第一值/get_all 等来源用途，并增加从同一列表计算的组合读取和排序投影。native Headers.get 使用组合读取；getSetCookie 和迭代中的 Set-Cookie 项保持独立。不存在第二份 map 或 JS headers 状态。

`LiveRequestTable` 草案和原 16+新增 4 项领域测试保留，尚未被生产 runtime 消费。事务修复已经落地：open 先准备日志和请求、插入成功后提交；redirect 先追加新 hop 再更新旧 hop；complete 先核验关联日志再切换终态，活跃日志以 pin 防止清空。active≤64、retained≤1024、hops≤20、URL≤16 KiB、累计 URL≤16 MiB，日志≤4096条/16 MiB。新增 13 项领域测试与原 20 项一同通过。这些是领域状态约束，实时 transport 接线及其全局资源预算继续独立实施与验收。

Cookie 仓库、CORS、页面 Promise/XHR 和 worker IPC 仍不在本轮。原生产 BrowserContext/JavaScript 中分散的资源与日志、PlatformApis 的简单 cookie 字符串属于来源债务，不能当作已经收敛的 live-network 实现。

## 首轮生产契约与所有权

| 项目 | 契约 |
| --- | --- |
| 资源目录 | 唯一 aggregate；每 Context 独立，运行期间允许 put/clear；get 返回独立快照 |
| 宿主入口 | `op:resource`，action=put/get/status/clear；Python/Go 薄客户端与原始 C 共用此入口 |
| put | url 为资源 key，body 为 `{kind:"text"或"bytes",base64:"..."}`，可选 status=200、headers=[]；完整验证再原子替换同 key |
| get | 同一 key 形态，exact 优先再去 query；返回 `{url,body,status,headers}` 或 null；不写浏览器请求日志 |
| status / clear / put 返回 | `{resources,bytes}`；bytes 为领域保留的 key/body/header 元数据计费，成功 clear 归零 |
| 文本与字节 | 两种 kind 均使用严格 RFC4648 canonical base64，拒绝 whitespace、非法 padding/pad bits；text 额外严格 UTF-8，保留 NUL，拒绝 surrogate 编码 |
| 目录配额 | key≤16 KiB，资源≤10000，body≤8 MiB，header list≤256 fields/64 KiB，目录总计费≤64 MiB；先验证/预留，失败保持旧值 |
| Headers 状态 | 同一领域有序列表，native constructor、prototype、方法、iterator 只有一个 realm installer |
| Header 配额寿命 | 每 list≤256 fields/64 KiB；每 context 同时存活 Headers≤10000、iterator≤10000、字段≤100000、Latin1 字节≤16 MiB；delete/失败/GC/close 对称释放；arena 独立于 realm coordinator 寿命 |
| 安装与释放 | Scheduling 后安装 Resource；final donor drain 时全部有效，随后 Resource realm/目录先于 Scheduling 释放 |

8 MiB 任意文本若直接进入 JSON，NUL 等字符会使返回值超过现有 16 MiB frame，因此 text 同样使用 base64，不以输入字符数代替 wire size。客户端 Python 方法为 `put_resource/get_resource/resource_status/clear_resources`，Go 对应 `PutResource/GetResource/ResourceStatus/ClearResources`；只转换 DTO，不存目录或另做匹配。未知字段、无效状态、非法 UTF-8/base64/header 在任何目录更新前失败。

资源 key 是显式、不含未配对 surrogate 的 Unicode 字符串；本阶段不将它解释为浏览器标准 URL，不用于 origin/cookie/redirect。资源调用不冻结 Environment/Scheduling，也不执行宏任务；现有 Reply 仍执行同一 engine job checkpoint，文档不承诺这些请求完全没有微任务观察边界。

目录原子性覆盖提交前的 schema、转换、HTTP 校验、配额与候选分配失败。提交后的结果对象分配、编码、engine checkpoint 或传输断连若失败，已经提交的 put/clear 保留；错误响应不承诺本次未提交。调用者可在会话仍存活时读取 status/get 确认，或重试同 key 的完整 put/clear。协议不宣称跨传输的 exactly-once 事务。

Headers 实现 donor 的 ByteString、HeadersInit sequence/record、append/delete/get/getSetCookie/has/set、entries/keys/values/forEach 和 iterable。get 缺席返回 null，已存在空值返回空串。迭代每次读取当前排序投影/长度，保留 live mutation 和 done 后 cursor；一般同名合并，Set-Cookie 保留单项。forEach(value,key,self) 使用显式 thisArg，异常立即传播。ByteString 检查 UTF-16 code unit≤255，再做 HTTP token/value 校验；不经 UTF-8 冒充单字节转换。

constructor/newTarget、prototype、brand、invalid receiver/Proxy、native identity、GC 及数组原型污染都由专用 adapter 验证。iterator 用 traced 引用保持 Headers；Headers 原生值记录与纯 native quota arena 在 finalizer 释放，不使用全局持久 roots 保留所有实例。finalizer 不读取 JS 属性，不依赖已销毁 Realm；方法仍只在所属引擎线程调用。

HeadersInit 的临时转换 draft 另受 256 个外层 tuple、每 tuple 256 个值和累计 64 KiB ByteString 限制；它保留限额内先完成全部转换、再验证 tuple 长度与 HTTP 字段的 donor 顺序。超配额明确 RangeError，临时 draft 在失败返回时释放，不占已发布实例的持久额度。这些数量属于产品资源边界，不宣称普通 Firefox 使用相同配额。

新增源按 model bridge、resource 用例、Headers factory/conversion/method/iterator 与 runtime control 分拆；单文件新增≤200行。原来源测试保持完整目的，额外领域测试单列 provenance。只有 model*.cpp 编译为异常保护桥；domain 没有 JSAPI/JSON/客户端依赖。实时 request/cookie/Body/worker 仍在后续同一 owner 扩展，URL、Scheduling completion、Abort/Streams 等依赖分别按独立领域检查点完成。

RPC 的 base64、UTF-8 检查及暂存分配由 `runtime/resource_buffer.cpp` 单独负责；它是无 JSAPI 的协议字节转换器，启用 C++ exceptions 并在 noexcept 入口捕获，返回同一静态错误 DTO。JS 字符串和 JSON schema 的 native 适配保持禁用异常。此职责不进入 domain，也不安装页面 atob/btoa 或第二个浏览器 API。

Necko 是指定的网络 transport adapter。它的 socket、NSS/TLS、HTTP 协议连接和流状态属于基础设施连接生命周期；浏览器 cookie、页面 origin、导航/子资源关系、CORS 和资源仓库仍由既定语义 owner 管理。会话 cookie 数据不能同时长期存放于领域和另一份 Necko Session jar；后续接口设计须明确可撤销的请求 DTO、cookie 提交与 redirect 边界。

## 当前只读核验

| 项目 | 结果 | 证据 |
| --- | --- | --- |
| 包版本 / ABI | 0.8.1 / 1 | artifact-manifest 与 DLL 版本函数 |
| NSS / NSPR | 3.130 / 4.40 | `necko_tls_nss_version` / `necko_tls_nspr_version` |
| 包清单 | 40 个文件实际 SHA 与 manifest 一致 | `artifacts/necko-inspection/report.json` |
| SpiderMonkey 配套 mozglue | 与 Necko 配套文件 SHA 不同 | 同一核验报告 |
| Python 依赖 | 项目内私有 CPython 3.14.6 与 14 项固定版本依赖 | `vendor/necko/manifest.json`；从本机缓存离线建立 |
| HTTP/1 请求 | 独立 adapter 的 5 次本地请求通过 | `artifacts/necko-inspection/vendor-probe.json` |
| NSS TLS / HTTP2 | 13 项本地传输检查通过 | `artifacts/necko-inspection/tls-h2-probe.json` |
| HTTP3 | 真实 H2 Alt-Svc → H3、NSS/Neqo 与 QUIC stream 通过 | `artifacts/necko-inspection/h3-probe.json` |
| 依赖搬移 | 1039 文件哈希复核一致；从新目录重跑 HTTP/1 通过 | `artifacts/necko-inspection/transport-validation.md` |
| JS 接入 | 待实现与验收 | 独立传输测试不计为 fetch/XHR 交付 |

Necko 配置来源为 Firefox 159（profile `firefox-159`，Neqo 0.32.0），与当前 SpiderMonkey 159 一致。TLS/HTTP profile 的版本要单独记录，不随 JS 引擎版本名称自动升级。Necko 的旧 `necko_http_perform*`/client C API 在 HTTPS 下仍走旧 WinHTTP 路径；指定 NSS 实现使用 `necko_stream.h`、`necko_tls.h` 及配套协议逻辑。不得因旧 C API 接入方便而把它当作已复用完整 NSS 路径。

## 协议实现与隔离

指定包的 HTTP/1 framing/socket 在 native core，TLS 在 NSS；HTTP/2 使用配套 Python/hyper-h2，HTTP/3 使用 Neqo/`necko_quic.dll` 并由配套包协调。证书链与主机名验证使用 cryptography verifier、包内 Firefox roots/constraints/public-suffix 数据。它不是完整 Gecko Necko，也不包括 Firefox 的全部 mozpkix/CT/OCSP/CRLite、DNS/cache、CORS 或页面调度能力。

为保留这套已有协议实现，并隔离不同版本同名 `mozglue.dll`，优先设计独立的内部 Necko transport 进程，由 `js.exe` runtime 的资源服务统一持有和关闭。该进程只传输有界 request/response DTO 与流块，不创建 DOM、realm、fetch wrapper 或另一套浏览器环境。最终打包需包含经过锁定的解释器/依赖闭包或明确、验证过的运行依赖，不以开发机隐式 site-packages 作为可移植交付。

现有私有目录使用 `python314._pth` 与 `-I -B` 隔离模块路径；实测所有第三方导入及 python314/necko_core/nss3/mozglue DLL 均来自 vendor。HTTP 探测直接调用 `HTTPAdapter.send(PreparedRequest, stream=True, ...)`，禁止 Session 构造，以避免另一份会话 cookie jar。重复 Set-Cookie 保留，下一请求不自动带 Cookie；显式 owner Cookie、二进制 POST、302 不自动跟随和同 TCP 连接复用均已有证据。Response 自身的临时 cookie 解析结果不作为浏览器持久状态。

TLS/H2 fixture 记录 NSS backend、ALPN 与实际协议版本，覆盖自建 CA、错误主机名、不受信任证书，H1/H2 下 gzip/br/zstd 解码，以及 H2 同连接重叠 stream、单流取消后另一请求继续、header 超时、未消费 H1 body 关闭后的新连接。第一次 IPv4-only localhost fixture 的失败单独保留，后续双 loopback 监听消除地址选择问题；未修改 vendor 协议源码。真实 H3 fixture 已验证 H2 Alt-Svc 切换、NSS/Neqo backend、ALPN h3、二进制 body 和重复 header。私有目录物理搬移后模块和相关 DLL 均来自新目录。上传/全阶段取消、H3 取消、总内存预算、干净系统分发与产品回放仍待验收。

C/Python/Go 调用者均通过同一个 `js.exe` runtime 使用此 transport；不能只让 Python 客户端联网而让 C/Go 走不同实现。原生浏览器 API 仍由 SpiderMonkey 所属 realm 工厂安装。共享 Scheduling owner 接收完成事件并驱动 Promise/XHR 状态，transport 工作线程或进程不得触碰 JS handle 或另建页面 job queue。

正式实现前需固定消息字段、二进制 body 的分块/上限、取消与超时、连接共享、错误映射、进程关闭和离线 bundle 优先规则。若出现外部包修复需求，先在本地独立副本记录来源与变更，不写回 donor 或复制未经说明的降级实现。

## 验收要求

原 Resource Network 16 项 GTest 已有原始成功基线，迁移后要在同一领域语义下再次运行。原 fetch/XHR/页面加载/回放的完整测试目的继续保留；不同请求入口必须复用同一资源 owner、调度与 transport。

本地实际服务器测试涵盖 HTTP/HTTPS、NSS 版本、证书与主机名失败、自定义 CA、header 重复与顺序、Unicode/二进制 body、redirect、压缩、超时、取消和连接关闭；HTTP/2 与 HTTP/3 分别提供真实协商和并发证据。离线 bundle 模式应能独立重放，明确记录是否发出了真实请求。协议版本、TLS backend、Firefox profile 来源和 JS 引擎身份均进入报告。

所有验证使用实际重建的 `js.exe` 与 C ABI DLL，完成 C/Python/Go 一致性、完整 Go 回归和关闭资源检查。最终同部署未插桩回放与文档源码核对仍是独立交付门槛。当前包 manifest/ABI 成功只证明本地输入完整、DLL 可载入。

## 检查点：Response 与 Body 混入（T7 第一片，checkpoint `align-fetch-response`）

状态：2026-10-06。任务书 T7 的第一片落地：真实 `Response` 构造器/原型与 Body 混入。`Request`、`AbortController`/`AbortSignal`、`fetch(Request)` 属 T7 后续切片，尚未安装。

- 有界上下文：仍是 Resource Network 一个域。页面 `fetch` 走既有的 `PerformHttp`（唯一 `Model`/`ResourceBundleAggregate` 入口），没有第二条资源路径或缓存。
- 状态来源：fetch 响应体来自 resource owner 返回的 `HttpResult`（字节与 `BodyKind`）；`new Response(body, init)` 的体来自构造参数。两者都只在建对象时复制一次进 `BodyRecord`。
- 对象工厂：`response_object.cpp` 是 `Response` 的唯一 owner——`kResponseClass`（7 个保留槽：0 `BodyRecord*`、1 headers、2 status、3 statusText、4 url、5 type、6 redirected）、`ResponseNew`、`ResponseObject`、原型字段访问器与 `clone()`。`response.cpp` 只做参数归一（构造器与 `json`/`error`/`redirect` 静态方法），两者共用一个 `Create`。
- Body 混入：`body.cpp` 是 `text`/`json`/`arrayBuffer` 与 `bodyUsed`/`body`/`headers` 访问器的唯一 owner；Request（后续切片）与 Response 共用这一份一次性消费语义（第二次读取以真实 `TypeError` 拒绝）。
- Headers 复用：响应头不是新容器，而是经 `factory.cpp::HeaderObject` 用同一个 `kHeadersClass` 生成的 `Headers` 实例，`response.headers instanceof Headers` 为真。
- 生产 installer：`realm.cpp` 每个 realm 只按 `InstallHeaders → InstallResponse → InstallPage` 顺序装配一次；`InstallResponse` 建构造器（`JSFUN_CONSTRUCTOR`）与原型并登记 Trace，`InstallBody` 只在混入的原型上装一次。无 `if (!window.X)` 守卫。
- 接口一致性：C ABI 与 Python 共用同一装配协调器，没有各写一份 bootstrap。
- 测试：`tests/test_fetch_response.py`（21 例）覆盖构造器/原型身份、`new` 要求、字段默认值与 init、状态范围、空体状态拒绝有体、`Response.json/error/redirect`、Body 一次性与 `clone`、`text/json/arrayBuffer`、非法 receiver、fetch 的异步性与 NetLog、响应头来自资源而非请求 init。
- 偏离：D26（无 ReadableStream、`clone()` 共享 headers 对象）。

证据：checkpoint `align-fetch-response` 的 `validation.json`。

## 检查点：Request 与 AbortController/AbortSignal（T7 第二片，checkpoint `align-fetch-request`）

状态：2026-10-06。任务书 T7 的第二片落地：真实 `Request` 构造器/原型、`AbortController`/`AbortSignal` 与 `fetch(Request)` 同路径。

- 有界上下文：仍是 Resource Network 一个域。`fetch(url, init)` 与 `fetch(request)` 共用同一 `RequestInput` 归一和同一 `PerformHttp`；没有第二条资源路径、缓存或请求描述。
- 状态来源：`RequestDraft`（`request.h`）是唯一的请求描述（method/url/接收体/header 对/模式字符串/keepalive/AbortSignal），由 `RequestInput` 与 `ReadRequest`（`clone()` 复用）产生。请求体字节经 `BodyRecord` 在对象与 fetch 之间传递，不重新解释文本。
- 对象工厂：`request_object.cpp` 是 `Request` 的唯一 owner——`kRequestClass`（13 保留槽：0 `BodyRecord*`、1 headers、2 method、3 url、4 mode、5 credentials、6 cache、7 redirect、8 signal、9 referrer、10 referrerPolicy、11 integrity、12 keepalive）、`RequestNew`、`ReadRequest`、字段访问器、`clone()` 与惰性 `get signal`。`request.cpp` 只做参数归一（输入 URL 或 `Request` + init 字典）与构造器安装，两者共用一个对象布局。
- Abort owner：`abort_signal.cpp` 拥有 `AbortSignal`（`kAbortSignalClass`，2 保留槽：aborted/reason；原型链 `AbortSignal.prototype` → `EventTarget.prototype`；`abort` 事件经 interaction 分发树派发；默认 reason 由页面 `DOMException` owner 构造）与 `AbortSignal.abort`；`abort.cpp` 拥有 `AbortController`（1 保留槽：signal）与 `abort()`。没有第二份 abort 状态。
- Headers 复用：`Request.headers` 与 fetch 的请求头都经同一个 `kHeadersClass` 生成，不新建容器。
- 生产 installer：`realm.cpp` 每个 realm 只按 `InstallHeaders → InstallResponse → InstallAbort → InstallRequest → InstallPage` 装配一次；构造器均带 `JSFUN_CONSTRUCTOR`，`Request`/`AbortController` 无 `new` 抛 `TypeError`，`AbortSignal` 由其全局构造器抛 `Illegal constructor`。无 `if (!window.X)` 守卫。
- 接口一致性：C ABI 与 Python 共用同一装配协调器，没有各写一份 bootstrap。
- 测试：`tests/test_request_abort.py`（25 例）覆盖构造器/原型身份、method 归一与非法 method、GET/HEAD 带体、文本/类型化数组/ArrayBuffer 体、init 字段与 header 对、从 `Request` 复制并转移 body、`clone()`、非法 receiver、惰性且稳定的 `signal`、abort 幂等与原因、`throwIfAborted`、`AbortSignal.abort`，以及 `fetch(url,init)`/`fetch(request)` 同路径同 NetLog、abort 拒绝且无日志。
- 偏离：D26（无 ReadableStream，`body` 为 `null`）、D27（header 折叠/`keepalive`/默认 `content-type` 边界）。

证据：checkpoint `align-fetch-request` 的 `validation.json`。

## 检查点：Response/Request Body 的 blob() 与 formData()（T7 第三片，checkpoint `align-fetch-body`）

状态：2026-10-06。任务书 T7 第三片落地：Body 混入补上 `blob()` 与 `formData()`，A7.4 至此完整。

- 有界上下文：仍是 Resource Network 一个域。体仍来自 `BodyRecord`（`body.cpp` 唯一一次性消费语义），没有第二条资源路径或体缓存。
- 对象工厂：`body_blob.cpp` 拥有 `blob()` 与共享的页面构造/MIME helper（`PageConstruct`/`HeaderValue`/`MimeType`/`MimeEssence`）；`body_form_data.cpp` 拥有 `formData()`；`multipart.cpp` 是唯一的 multipart/form-data 纯解析器（不含 JSAPI）。三者都不新建浏览器对象：`Blob`/`File`/`FormData` 一律由页面 realm 的构造器创建，urlencoded 直接遍历页面 `URLSearchParams`（唯一 urlencoded parser）。
- 生产 installer：`InstallBody`（`body.cpp`）在 `Request`/`Response` 混入的原型上装一次 `text`/`json`/`arrayBuffer`/`blob`/`formData`；安装顺序与构造器 owner 不变（`InstallHeaders → InstallResponse → InstallAbort → InstallRequest → InstallPage`）。无 `if (!window.X)` 守卫。
- 接口一致性：C ABI 与 Python 共用同一装配协调器，没有各写一份 bootstrap。
- 测试：`tests/test_body_binary.py`（9 例）覆盖 `blob()` 的 size/type（MIME essence、无 content-type 为空）/内容、一次性消费、非法 receiver；`formData()` 的 urlencoded 有序解析（`+` 还原、重复键）、缺 content-type 回退、multipart 的字段与文件（`File` 名字/type/size/text）、缺 boundary 以 `TypeError` 拒绝、一次性消费与非法 receiver。
- 偏离：D26（无 ReadableStream、`clone()` 共享 headers）、D32（MIME essence、multipart 子集、urlencoded 复用 URLSearchParams）。

证据：checkpoint `align-fetch-body` 的 `validation.json`。

## 检查点：fetch 的 C ABI/Python 一致（T7 第四片，checkpoint `align-fetch-cabi`）

状态：2026-10-06。任务书 T7 第四片（A7.8）：`tests/test_fetch_cabi.py`（1 例）用原始 C ABI（`resource put`/`eval`/`resource log`）与 Python `Context` 对同一条 `fetch` 请求断言相同的 `Response` 字段与逐条相同的 NetLog。它不新增 owner 或接口，只证明两条客户端共用同一装配与同一资源路径。

证据：checkpoint `align-fetch-cabi` 的 `validation.json`。

## 检查点：子 realm 的资源面（T25 子项⑤，checkpoint `align-iframe-realm-resource`）

状态：2026-10-06。任务书 T25 第 5 项（A25.3/A25.5/A25.6）：iframe 子 realm 得到自己的 `fetch`、XHR、`Headers`/`Response`/`Request`/`AbortController`/`AbortSignal`/`Storage` 与 `localStorage`/`sessionStorage`/`document.cookie`，背后仍是**同一份**资源目录、**同一条** NetLog 与**同一个** Web Storage owner。

- 有界上下文：仍是 Resource Network 一个域。子 realm 不新建目录、日志或存储：`Model::ShareFrom(parent)` 让子 `Model` 指向 owner 的那一个 `ResourceBundleAggregate`（`Impl::shared`，只有 owner 构造它），`State::store` 直接复用 owner 的 `StorageArea`（`State::owns_store=false`，只有 owner 在析构时释放）。子上下文继承创建者的源，因此 `localStorage`/`sessionStorage`/cookie 与创建者共享（A25.6）。
- 唯一 installer：`Realm::InstallSurface` 是那一条有序安装序列（`InstallHeaders → InstallResponse → InstallAbort → InstallRequest → InstallPage`），owner realm 的 `Realm::Install` 与子 realm 的 `Realm::InstallInto` 都调用它，每个 realm 只执行一次；没有第二份安装列表，也没有 `if (!window.X)` 守卫。每个 realm 各有一份 Headers/Response/Request/Abort 对象与一个引用计数的 header arena，是「同一断言、同一代码在多 realm 执行」，不是第二个 owner。
- 装配点：`Frames::Create` 在建好子上下文、任何子脚本可运行之前，经 `Frames::SetResourcePort` 调 `Realm::InstallInto(*child->helpers, *child->interaction)`；子 realm 的 `Realm` 存在 `BrowsingContext::resources`（最后一个成员，先于 Interaction 释放）。owner realm 在 `runtime::Run` 里**声明在 `Frames` 之前**，所以它晚于所有持有共享视图的子 realm 释放；`FrameLoadPorts` guard 在 `Run` 的 `Frames` 之前解绑端口。
- 测试：`tests/test_iframe_realm.py` 的 `IframeRealmResourceTests`（3 例）断言资源面身份与 realm 归属（`w.Headers !== Headers`、`w.fetch !== fetch`、`w.localStorage !== localStorage`、子 `Response` 实例 `instanceof w.Response` 而非主 realm 的）、子 realm 的 `fetch` 与同步 XHR 命中主 Context `put_resource` 放入的同一个条目并写同一条 NetLog、`localStorage`/`sessionStorage`/cookie 与创建者共享（写一边读另一边可见、`clear()` 两边同时生效）。

证据：checkpoint `align-iframe-realm-resource` 的 `validation.json`。

## 检查点：debug 监控上报点（T24 第四个 checkpoint，checkpoint `align-debug-monitor-resource-network`）

状态：2026-10-07。任务书 T24 的 Resource Network 域：debug 模式下本域 installer 登记的真实 native 成员被读取/调用时产生记录，路径为 `Headers.get`、`Storage.length`、`Window.fetch`、`Request.clone` 等；默认模式零记录，且逐项不改变本域可观察行为。

- 端口：`resource_network::ApiMonitorPort`/`ApiAccess`（`realm.h`），与 Document Model/Interaction/Browser Environment/Execution Scheduling 同形；域不引入 observability 依赖，`State::Observe` 在端口为空时直接跳过。
- 装配：`runtime::Run` 的 `MonitorBinding` guard 在 debug 模式把端口绑到 `resource_network::Realm`，作用域结束前解绑；端口绑在唯一的 Context 级 `Realm` 上，子 realm 的 `InstallInto` 造自己的对象但共享同一个 `State`，端口未绑定，因此只记录主 realm。
- 上报点全部在 receiver 校验之后：Headers 方法/迭代器/构造、`Window.fetch`、XMLHttpRequest `open`/`send`/构造、Storage 方法/`length`、AbortController 与 AbortSignal 的读/调用、Request/Response 的字段/`clone`/静态方法与 Body mixin。Body mixin 的 native 在 `Consume` 之前先取 owner 与接口：`CallArgs` 的 callee 槽与返回值槽是同一处（`vp[-2]`），已消费路径写回 rejected promise 会覆盖 callee。
- 测试：`tests/test_debug_monitor.py` 的 `ResourceNetworkMonitorTests` 12 项，覆盖 A24.1–A24.7 与 C ABI 一致性。

证据：checkpoint `align-debug-monitor-resource-network` 的 `validation.json`。

## 2026-10-17 逐跳交换与异步传输（ARCHITECTURE「图片异步获取」）

`page_http.cpp` 的跳转循环拆为 `HttpExchange`（`http_exchange.h`）：`Next()` 准备一跳（默认头、Cookie、NetLog 记录，blob/资源目录
在本线程完成），`Deliver()` 写回 Set-Cookie、计时与重定向策略。`PerformHttp` 仍同步驱动它，脚本/导航/XHR/fetch/样式/信标的
行为与 NetLog 不变。`async_http.{h,cpp}` 的 `AsyncHttp` 是 Context 级 owner：每跳只把 `NeckoHttp` 交给后台线程（复制的输入，
在途上限 16），其余步骤与完成回调在主线程 `Run()` 中执行；当前唯一使用者是图片获取。`necko_http.cpp` 改为按 origin 各一个
会话子进程（最多 6 个），不同 origin 并行，同 origin 串行。

## Firefox XHR `mozAnon`/`mozSystem`（状态来源阻塞记录，2026-10-18）

Firefox 基线把 `XMLHttpRequest.prototype.mozAnon` 和 `mozSystem` 记录为只读
accessor（`get`，无 setter，enumerable/configurable 均为 true）。现有 XHR 状态
尚无可为这两项提供真实值的来源：`XhrRecord` 只有 `XmlHttpRequestState` 的
ready/pending/asynchronous/credentials 等字段，`XhrConstructor` 尚未读取构造
参数；`HttpOptions` 尚未表达 anonymous/system 请求策略。固定返回 `false` 会把
未建模的构造选项和 transport 语义误映射为具体值；本记录将差异登记为待补状态项。

- 有界上下文/状态来源：仍归 Resource Network。待设计的 XHR 请求描述需在
  `domain/resource_network/xml_http_request.h` 增加构造时捕获的选项，并由同一
  `HttpOptions`/`PerformHttp` 路径消费；所有状态继续归入现有 XHR owner。
- 工厂/安装器：唯一生产路径保持 `xhr_object.cpp::XhrConstructor`、
  `page_xhr.cpp::InstallXhr`、`xhr_members.cpp::XhrGet`。完成实现时应在该路径
  注册两个只读 accessor，并让非法 receiver、跨 realm 和 GC 继续走现有品牌/工厂。
- 语义边界：`mozAnon` 对 cookie/凭据发送与接收的影响须由现有 Cookie/CORS
  owner 和 transport 证据确定；当前 `credentials` 布尔值仅表达凭据模式，未表达
  `mozSystem` 的 system-principal/跨源策略。任何字段扩展都要同步 C ABI/Python
  的共享装配和 worker 请求 wire（如适用）。
- fixture/验收计划：补 Firefox 与本产品同脚本，覆盖默认及构造参数组合、
  getter 值、只读赋值、descriptor、`open`/`send` 的 cookie 与跨源结果、
  非法 receiver、iframe realm、GC，以及 C ABI/Python 同路和 NetLog。只有
  这些结果锁定状态来源后，再实施字段、`HttpOptions` 和 accessor；当前差异
  保留为 `resource_network` owner 的阻塞项。

## cookieStore (2026-10-20)

`window.cookieStore`（`dom/webidl/CookieStore.webidl`）在既有 cookie jar 上实现，行为以 Firefox 158
本机探针为准（`artifacts/fp-trace-parity/cookiestore_probe.js` 对 `cookiestore/firefox.json` 24/24 全等，
含异步事件序列）。

- 有界上下文/状态来源：仍是 Resource Network 的 Context 级 jar
  （`domain/resource_network/cookie_store.h`）。领域新增 `CookiePair`/`CookieChanges` 与
  `Matches()`（与 `Joined()` 共用私有 `Select()`：同一 host/domain/path/Secure/httpOnly 过滤与
  path 长、创建序排序），`Assign()` 增加可选 `CookieChanges*` 出参报告写入/删除；既有调用保持默认实参
  不变。`model_storage.h` 只增加 `CookieQuery` 与 `CookieAssign` 的转发结构（`CookieEntry`/
  `CookieChangeSet`），`StorageArea` 仍是唯一持有者，未新增第二个 jar 或状态。
- 唯一生产 installer：`InstallCookieStore`（`page_cookie_store.cpp`），由 `InstallStorage` 在既有
  `InstallCookie` 之后调用，仍属 `Realm::InstallSurface` 那一条有序序列；主 realm 与 iframe/后续顶层
  文档 realm 都走这条路径，各自得到自己的 `window.CookieStore`/`window.cookieStore`
  （`State::cookieStore`）。接口对象经共享 `WebKit::Define` 建立，父接口为 Interaction 的
  `EventTarget`；实例登记为 Interaction 目标（`Realm::Attach`），因此 `instanceof EventTarget` 与
  `addEventListener('change')` 走同一条派发树。
- 成员与语义：`get`/`getAll` 返回只含 `name`/`value` 的普通对象数组（`CookieItem`），`get` 无匹配时为
  `null`；`set(name,value)`/`set(CookieInit)`、`delete(name|CookieStoreDeleteOptions)` 都构造
  `Set-Cookie` 文本后走 `CookieAssign(http=false)`——与 `document.cookie` 同一解析器与同一 jar；默认
  path `/`、sameSite `strict`、无 expires 为会话 cookie，删除即 `Max-Age=0`。参数转换与校验在
  `page_cookie_options.cpp`，规则与 Firefox 文案在 `page_cookie_rules.cpp`（path/domain 判定取自
  `domain/resource_network/cookie_scope.h`）。非法 receiver 的 promise 拒绝、
  `CookieStoreGetOptions must not be empty`、`0 is not a valid argument count for any overload.`、
  `Argument 1 can't be converted to a dictionary.`、name/value/path/domain/sameSite 文案与
  `"<url> is not a valid URL."` 逐字一致。
- 变更事件：`CookieChangeEvent` 仍是 Interaction 的派生事件（未建第二份）。`document.cookie` 写入与
  `set`/`delete` 的 `CookieAssign` 变化集由 `page_cookie_events.cpp::PostCookieChange` 组成
  `{name,value}`/`{name}` 两个列表，作为**任务**投递（`Realm::SetCookieTaskPort`，由共享装配绑定
  Context 级 `PostTask`，子 realm 从创建者继承），任务内调用 Interaction 新增端口
  `Realm::FireCookieChange`（`changed`/`deleted` 存在事件的 `kEventPublicFirst/Second` 槽，getter 直读）。
  `onchange` 是事件处理器属性：安装时捕获 EventTarget 的 add/removeEventListener，赋非 callable 变 null。
- [SecureContext]：`SecureSlots` 登记 `window.cookieStore` 与 `CookieStore`，`Realm::ConfigureExposure`
  由 `runtime/secure_exposure.cpp` 通过 `BrowsingContext::navigatorNetwork`（主页面与子框架都指向本
  上下文的资源 realm）调用；`CookieChangeEvent` 加进 Interaction 的 SecureContext 名单。
- 验收：`python -B probe.py cookiestore_probe.js __cs cookiestore --skip-firefox` 24/24；
  `run.py --tag ds2` + `diff.py` 由 1360/19 到 1362/17（`cookieStore` 组与 `async.cookieStore.get` 消失，
  其余差异与基线相同）。时间源仍借 Scheduling 的 cookie clock 端口，未引入第二时钟。

## WebSocket 最小 trace 面（2026-10-20）

WebSocket 归 Resource Network owner。`websocket.cpp`/`websocket_install.cpp` 是唯一生产工厂与 installer：
`WebKit::Define` 在每个 realm 建立一个 `WebSocket` 构造器和以 Interaction `EventTarget.prototype` 为父的原型，
并登记构造器 trace。实例由该构造器创建、挂入同一事件树，URL 与连接状态保存在实例 reserved slots；没有第二套
网络目录、连接池或事件分发器。`onerror` 访问器把 callable 值委托给同一 `addEventListener("error")`/
`removeEventListener("error")`，其他值按 WebIDL 规则读回 `null`；`close()` 只更新该实例的连接状态为 `CLOSED`
并复用 receiver 校验，传输握手仍属于后续 Resource Network slice。

Firefox 158 focused trace 验收目标：构造器 `length=1`、`WebSocket.prototype.onerror` 为 enumerable/configurable
的 getter/setter、`close` 为 enumerable/configurable/writable 的零参数函数、`new WebSocket("ws://...")`
返回 `[object WebSocket]` 且 `onerror` 初始为 `null`，以及非法 receiver 抛 `TypeError`。本片不声明完整 WebSocket
传输能力；未实现成员继续保持缺席。



## S1.5: the original read FIFO accepts a completion target (2026-10-21)

`readable_stream.h::ReadRequestSlot` renames QPromise to QTarget at the same
numeric slot. Original SHead/STail is still the only pending-read list and
original byte descriptors still pair with that list. Public requests store
the same Promise with no additional allocation. `ReadDefaultReaderInto`
extracts the existing status/disturbance/default-byte/native dispatch for
either target; S1's public ReadDefaultReader creates the public Promise.

`read_completion.{h,cpp}` owns the internal result port. Public targets call
the original ResolveRead/ResolveByobRead/RejectPromise operations, keeping
result properties, then assimilation and realm behavior. Native records are
private traced objects with owner and operation slots plus a pointer to an
immutable static ReadCompletionSteps table. The factory validates owner,
operation and every required callback. Tables contain no JS edge or State.
Callers must supply static-lifetime tables; the record does not own a table.

The original read queue, byte commit, byte queue, byte terminal, close and
error sites all dispatch through that port, retaining their existing public
mutation/failure order. There is no alternate queue, state machine, cursor or
public surface. An operation record has no independent stream status.

No native table consumer exists in this slice. Later tee/Body integration must
add actual callbacks atomically with the native reentrant prepare/pop/recover
rules at the original FIFO sites. It must not invoke a reentrant callback
while a request/byte entry remains published unless the original algorithm
explicitly requires it. The current candidate preserves public code ordering
and does not claim those future native delivery rules have been implemented.

Acceptance is strict public preservation: existing B1/R1/R2, Body initial
errors and exact S1 buffered before/after; `readable_request_public_probe.js`
adds ten default/byte/native immediate/queued/default/BYOB rows with two
observable read-result then lookups each, unchanged result keys and timing.
Firefox Window/Worker agree. Native consumer acceptance later includes the
dedicated tee and Body probes with zero internal result then lookups and
actual Window GC. Native Worker functional checks do not imply forced GC.


## T0 original factory responsibilities

`readable_source.cpp` remains the sole public underlying-source/strategy conversion owner. `readable_setup.cpp` owns the extracted allocation/setup tail for the existing default/byte controller, including its original start observation. `readable_factory.cpp::NewReadableStream` remains unchanged.

`writable_factory.cpp::NewWritableStream` is the sole allocation helper for the original writable class and is called by its public constructor. `writable_setup.cpp` retains the original public sink/strategy conversion. `writable_setup_controller.cpp` owns the extracted original sink controller/AbortSignal/queue/pressure/start initialization tail. Their signatures use rooted handles, and helpers execute in the caller's existing realm.

No class layout, pending operation, callback retention or clear-algorithms owner changes. The initializer functions do not reconvert dictionaries, consult replaced global constructors or synthesize source/sink objects. Any later native source/sink algorithm path must be one common protocol shared by tee and Transform, integrated at the original callback dispatch seam. T0 itself exercises only the unchanged public construction path.


## Callback entry lifecycle port

The owner receives a borrowed callback-global query from Runtime. A common native
entry helper uses the original captured callback, unwraps it without consulting
page properties, and queries the existing Scheduling retirement authority. It
returns a stack-only result distinguishing successful entry, rejected entry and
native failure. Rejected entry creates the existing installed DOMException;
failure to allocate that error remains native failure. The query helper adds no
callback storage and no page-visible API. Original callback adapters opt into
this helper explicitly so each algorithm preserves its own error propagation.

CallbackContext is invocation-local: message and optional exception owner carry
the binding method's exception realm. The controller's original signal owner
still supplies the lifecycle query. Borrowing a parent writer method for a child
writer therefore creates its entry DOMException in the parent realm without
changing which captured callback global is checked. No State pointer is retained
by an asynchronous reaction.


## Writable entry outcomes

CallbackResult travels on the C++ call stack through the existing sink call,
advance/erroring and native reaction boundaries. EntryRejected bypasses
WritableNativeFailure so it does not reject an internal request or clear a queue
whose donor algorithm returned early. Failure continues the original native
failure route; Success includes an entered callback's rejected Promise.

The query examines the captured callback's global, independently of stream
owner, receiver and active realm. The original KSignal/AOwner edge supplies the
resource query port synchronously. The existing start and size boundaries still
throw or error the stream where their donor algorithms require it. Error names
and page properties never classify an entry result. No new async State pointer,
detached bit, callback registry or second stream state machine is added.

A native observer returning EntryRejected leaves the pending exception for the
engine. The original engine creates a temporary rejected Promise when that
observer has no result capability. Original ExceptionCapture may report this
through RPC; verification preserves its raw name, message and stack separately
and reads page states afterward. This does not establish unhandledrejection
event-surface parity, and does not suppress host errors in product code.

## Native tee through original Streams owners (S2/S3)

ByteRespondInternal now calls the descriptor-only ProcessPullIntoQueue operation
extracted from the original byte queue. ByteEnqueue continues to use the original
reader-aware ProcessByteQueue. Both consume the existing controller queue and
descriptor list, and commit into its stream's sole pending-read FIFO. The
unchanged release/replacement tee donor exposed the overbroad respond dispatch;
the change adds no tee-specific reader state. Existing per-descriptor fill/commit
order is retained, without a claim of broader reentrant-order parity.

Native chunk task failures pass through the original bounded
CaptureOperationFailure port after the original algorithm has set its pending
exception. A successful capture returns normally to the intrinsic scheduling
reaction, avoiding a hidden rejection and duplicate report. Absent capture
propagates failure. This follows Scheduling's existing native microtask boundary;
original ExceptionCapture/Child::ReportError/Interaction still own reporting.
Window RPC mapping is measured separately from Worker error-event delivery.

The existing ResourceStreamPrototypes record retains its installed constructor
and controller/request prototypes. Branches are built in the source realm by
NewReadableStream and T0 InitializeReadableController. Source State is recovered
only synchronously from the validated original native constructor; asynchronous
operations hold traced JS handles and never State pointers.

The one tee operation contains source/current reader/two original branches,
one cancel Promise/two reasons/factories and numeric read/cancel coordination.
It owns no byte cursor, queue, stream status or reader lock. All reads use
S1.5 native completion targets in the existing source read FIFO. Default and
byte controllers continue to own each branch's queue/descriptors/transfers.

The common NativeSourceAlgorithms record lives in original DSource and carries
one traced operation plus a required static immutable start/pull/cancel table.
It is the common tee/Transform contract. Original callback dispatch separates
native typed outcomes from author Promise conversion. Original algorithm clear
releases this DSource edge; no new lifecycle or asynchronous owner is added.

Native completion callbacks run after consuming the original current request,
with target rooted; public settlement order remains unchanged. ByteClose's
invocation context now names Public/Buffered/Intrinsic to preserve original
author/native Body behavior and distinguish tee's internal error wording.
A stack-only ByteCloseOutcome reports the original incomplete-element predicate
to tee; expected EOF fragments retain their donor sibling behavior.
TeeRecoverFailure clears coordination and settles shared cancellation/branches
through original controllers on post-publication engine/native failures.

Public tee result arrays and validation errors belong to method realm; branches
and read/cancel Promises belong to source realm. Cancellation carries its method
global synchronously so the combined reason Array belongs to the second cancel
method realm. Native source.cancel executes once via original stream closure.

Shared cancel Promise never points back to tee operation. Source reader.closed
reactions check reader identity to ignore replaced byte readers. All graph
edges are normal traced slots/reactions; there are no instance PersistentRoots.
Dedicated Firefox 24-weakref evidence gates actual GC, separate from native
Worker functional checks. Default/byte/realm/intrinsic/edge donor checks and all
previous stream/Body preservation gates must pass before this slice is accepted.


## Original TransformStream implementation

Transform's constructor converts writable strategy, readable strategy and ordered
Transformer dictionary members, then validates types and watermarks. It creates
the existing writable controller before the existing default readable controller;
both native starts return one pending intrinsic Promise. Author start runs only
after the graph is initialized; its callable is not retained after the call.

Backpressure uses one replacement change Promise, with replacement allocated
before publication. The existing readable pull clears pressure and returns the
new Promise; existing W1 serializes writes. Enqueue delegates to the original
readable controller. Error/cancel/abort/terminate use original readable/writable
terminal paths, clear captured transform/flush, and release pressure waiters.
Flush obtains its Promise before algorithm clearing; native entry rejection
returns before that clear point. The callable's actual global controls new entry,
while already-entered callback Promises and native continuations can complete.

The native sink adapter mirrors the shared source record protocol and returns
CallbackResult without promisifying native failures. CallbackContext travels
only during one call so borrowed writer methods retain their error realm. The
pressure reaction explicitly resets to its own asynchronous entry context and
uses an original intrinsic result Promise. Normal JS throws still reject and
error both streams; entry rejection through pressure errors writable only.

The final authoritative slots are TReadable/TWritable/TController/TPressure/
TChange on Transform, and CTransform/CTransformer/CTransformCall/CFlushCall on
its controller. Shared native-operation slots TNStream/TNStart are retained only
by original DSource/KSink native algorithm records. Both native starts return
TNStart; every later native step takes TNStream. Neither Transform nor controller
has a reverse operation edge. ClearTransformAlgorithms clears author algorithms
only, while original source/sink clearing releases the start result independently.

Original NewWritableStream/InitializeWritableController run before original
NewReadableStream/InitializeReadableController. Original Writable AbortSignal,
reader/writer factories, source/sink queues, stream statuses and stored errors
remain their sole authorities. InstallTransformStreams replaces the old scaffold
in place; original Window and Worker assembly each call that one installer.
PageReference retains only the Transform fallback prototype, never instances.

TransformNativeFailure roots/preserves the first exception, tries DefaultError
and WritableNativeFailure even if either fails, clears author callbacks and
resolves the existing pressure Promise as emergency cleanup. It restores the
original pending exception. No finalizer calls JS; no asynchronous State capture
or new retirement authority exists. Normal callback rejection and EntryRejected
remain separate from this native failure path.

## Native Transform algorithm prerequisite

The existing Transform installer retains its original constructor through
PageReference, next to the constructor's fallback prototype. The original
controller CTransformer slot can own a branded native transform/flush record
with a traced consumer and immutable static steps. Original algorithm clearing
releases that record. Factory lookup validates the original constructor and
owner before the synchronous State borrow; initialization and returned streams
use the existing Transform/readable/writable factories and controller queues.
Native calls enter controller realm and use the original Promise/error path.
This ResourceNetwork prerequisite installs no codec or public API. Its strict
Transform and existing Stream/Body preservation evidence is recorded separately
from the subsequent WebPlatform TextDecoderStream consumer.

Independent gates cover behavior, surfaces, borrowed/newTarget realms, ordinary
and retired callback entry, Window/removed-iframe GC, and start-result payload
lifetime during open, flush-pending, close-output, start-reject and terminal.
Three acknowledged major GCs before and after release are required. Start-reject
payload retention follows original stored readable/writable errors. Actual native
Worker functional acceptance is separate from forced native Worker GC, which is
uncovered. Root integrates/builds and compares unchanged Python/C donor sources;
artifact syntax review alone claims no runtime or two-site acceptance.

## Native completion task-port lifetime (2026-10-21)

Original SetKeepaliveTaskPort still points to the one Scheduling owner. The
existing callback-binding scope guard now unbinds that port after the final
checkpoint for main, live/retired Frames children, and TopLevel later contexts.
The old XHR scope guard continues clearing its original HTTP/XHR/Cookie/Abort
ports but no longer clears keepalive early. Original UnbindResourceCallbacks
methods iterate the same retained owners and clear both callback-entry and
native completion ports. No per-operation state or cleanup algorithm is added.

Window ExceptionCapture is declared before both guards and every owner, and is
alive through final checkpoint and destruction. This mechanical prerequisite
does not add exception handling or execute JS from any owner destructor.
Preservation uses existing R1/B1/callback lifecycle and root module gates; it
does not claim an exit task-drain guarantee or new Worker teardown behavior.

# Cache domain ownership addendum (proposed, before code)

This is an artifact-only review proposal. It adds no browser surface and claims
no native Cache acceptance. Root review precedes candidate implementation.

| Responsibility | Sole owner / proposed extension |
| --- | --- |
| Resource authority | Original domain/resource_network::ResourceBundleAggregate receives CacheStorageState by value; original Model::ShareFrom borrows the same aggregate |
| Origin partition | CacheStorageState owns origin-keyed namespaces; input is the existing origin owner's canonical tuple origin, never an arbitrary request URL or shared `null` string |
| Names | One insertion-ordered namespace name list; names are DOMString UTF-16, including lone surrogates |
| Store / entries | Each named slot references one shared native CacheStore; its ordered immutable entry list is authoritative |
| Deleted store | Name deletion releases only that named reference; held native store handles retain the old store; reopening creates a distinct store |
| Entry snapshots | Native Request/Response value snapshots and HttpHeaders lists; no JS wrappers, BodyHolder, stream, reader, signal or replaced public function is retained |
| Matching | Pure domain URL/query/Vary/method rules over already normalized request snapshots |
| Transactions | Domain validates/builds a complete candidate entry list, then publishes by nonthrowing swap and counter updates |
| Bounds | Origin budget counts all live stores, including deleted but held stores; no implicit eviction; per-entry existing body/header/key limits remain enforced |
| Model boundary | Existing Status/ErrorCode and bounded value DTO convention; model_cache*.cpp catch exceptions; JSAPI files contain no throwing domain headers |
| Request DTO | Mechanical extraction of unchanged RequestDraft to request_value.h permits use by engine-independent bridge; no constructor or transfer path is called for Cache queries |
| Body | Original Body AcquireBody with AfterValidation mode; Cache has no second reader, unread queue, used flag, lock state, or source cursor |
| Factories | Original RequestNew, ResponseNew and HeaderObject reconstruct fresh values in receiver realm; installer remains original ResourceNetwork assembly |
| Worker | Original runtime WorkerSession composes an owner-specific network session; its private store handle table references the same parent stores |
| Worker lifetime | Session captures original origin and releases its handles with existing worker service teardown; stale or foreign handle IDs fail at that boundary |
| Clear / navigation | Model::Clear retains its resource-catalog-only behavior; existing context lifetime owns persistence; held handles never retarget after navigation |

Named references and native handles share store ownership, not entry copies.
Namespace budget bookkeeping is shared with its stores so removal from the name
list does not erase retained-byte accounting. It contains counters only, not a
second entry index. JS wrappers own one opaque handle and finalize it without JS.
OpenCacheScope is the realm/Worker-session origin-capture lifecycle operation and
publishes an empty namespace intentionally. Query misses add no names/namespaces.
The Context retains these scopes until teardown with explicit 10,000-origin and
16 MiB total origin-key limits; insertion commits bytes only after map insertion.

All candidate handwritten files stay below 200 lines, split by DTO, match,
transaction, namespace and bridge responsibility. Original flat source discovery
and model_ exception selection remain sufficient. The modified imported
resource_bundle.h needs an updated provenance entry at integration.

Original donor behavior has precedence over convenient generic rules. Vary
matching uses the stored Response's Vary, compares original Headers combined
values, treats missing and empty values equally in this Firefox donor, and
ignores Vary for opaque stored Responses. Batch duplicate detection is directional:
compare each later request against earlier pairs using those earlier responses.

Pending Cache operations retain Cache and the Body native completion target.
The original Body operation retains BodyHolder/source reader, never Response.
Completed storage entries retain only native snapshots. Native Window GC and
functional Worker coverage remain separate; forced native Worker GC has no
existing acknowledged control port and is not claimed.

## Streaming Response Body owner record (2026-10-21)

| Field | Authority |
| --- | --- |
| Body lifetime/bytes | Existing finalized BodyHolder/BodyRecord |
| Source variants | OwnBytes, NativeBytesAlias traced original holder, ScriptStream exact HStream |
| Lock/disturbance | Existing SReader and source BodyRecord.used/SDisturbed |
| Pending reads | One original stream FIFO native completion; no Promise result object |
| Continuation | Original keepalive task port and numeric acquisition phase |
| Result | Receiver-realm Promise/intrinsic factories; method realm for initial binding/unusable errors |
| Consumer | Typed target completion usable by Body and later Cache, no Response wrapper edge |
| Consumer MIME | Immutable native metadata moved at invocation, nothrow allocation and foreground cleanup |
| Retirement | Original Scheduling retired query plus existing Frames/TopLevel notification |
| Bookkeeping | Non-rooting intrusive original State list, raw owner for unlink only |
| Cleanup | Stack finish guard roots inputs, Terminal before settlement, head/root/unlink before GC |
| Factories | Original ResponseNew/HeaderObject/WebPlatform native factories and original TeeReadable |
| Request | Original conversion/upload/body exposure preserved |

Source error maps once from typed SourceFailure; wrong type cancels once with a
distinct error and does not await cancel. Retirement never cancels/releases the
source reader. Late original FIFO completions retain only a terminal operation
and return without touching owner/result state. Every retirement failure reports
through original ExceptionCapture, clears pending exception, then continues.
Native failure preserves original exception state/stack with AutoSaveExceptionState.
Settlement after allocation/wrapping failure is best effort: edges are cleared
even when the output Promise remains pending. The operation's static typed steps
do not retain State, wrapper or callback objects.

Original native bytes remain shared for aliases; constructor does not copy or
disturb them. Clone snapshots bytes into its independent original BodyRecord.
Script clone tees using original branches in source realm and wrapper in receiver
realm. Public StreamTee retains its result Array's caller realm and original
failure cleanup; extraction changes no public ordering or source algorithms.

Acceptance requires frozen independent input/clone/timing/realms/chunks/detach/
intrinsic/GC and Body errors, actual Python/C binary identity, all original
buffered/Request/tee/B1/R1/R2 gates and root site replay. Native forced Worker GC,
compacting GC and resizable chunk donor issue remain explicit boundaries.

Public consume on an already retired ScriptStream receiver uses the original
callback-entry query before creating its receiver-owned consumer. After the
existing used/locked check it marks local used, leaves the stream unlocked and
rejects in the method realm. Pending retirement still settles in receiver realm;
typed AcquireBody keeps its Unavailable policy for other native consumers.
Intrinsic form input and multipart filenames retain the original WebPlatform
UTF8 conversion rather than selecting ResourceNetwork's HTTP ByteString helper.


## Original native Transform factory and synchronous codec algorithms

Owner: ResourceNetwork's existing TransformStream and Transform controller.
State source: the original stream slots, controller queues and pressure Promise.
The existing CTransformer slot selects author callback metadata or a branded
native record; the native record holds only a traced consumer and static steps.
Original ClearTransformAlgorithms is the single release authority. The decoder
codec remains WebPlatform-owned in a later slice.

Factory: original TransformConstructor is retained by the original installer
through PageReference. The native factory validates native constructor identity,
native reserved fields and matching State before its synchronous borrow; it
does not retain State across jobs. It uses InitializeTransform and original
realm prototypes. Native enqueue validates the same controller class and calls
the existing TransformEnqueue operation. Window/Worker/C/Python share the
unchanged ordered ResourceNetwork installer.

Tests: exact original Transform behavior/surface/realm and callback lifecycle,
major GC plus old readable/writable preservation, actual rebuilt C DLL and
Python execution, module gates and Fangdi. TextDecoderStream's frozen donor
surface/behavior/realm/GC inputs will prove actual native-step consumption;
that downstream behavior is not claimed by this prerequisite alone. Historical
Go tests are absent from this checkout. New files have one responsibility and
stay below 200 lines; no split exemption is requested.

## Cache native adapter owner record

| Concern | Single production authority |
| --- | --- |
| Entries, origin namespace, ordering, atomic replacement | domain/resource_network CacheStorageState/CacheNamespace/CacheStore in existing ResourceBundleAggregate |
| DTO conversion and exception boundaries | model_cache*.cpp; matching never consumes Request Body |
| Native Cache/CacheStorage object and prototype | cache_factory.cpp original WebKit/ResourceNetwork State |
| Install ordering | existing Realm InstallSurface and InstallWorkerNetwork, after Request/Response/Streams |
| Secure exposure | original SecureSlots, Window and Worker |
| Origin source | original Environment::Origin port, captured by scope/session |
| Body acquisition | original AcquireBody AfterValidation; no Cache reader or consumption flag |
| Concurrent network | original AsyncHttp / HttpExchange / Necko; H2 FetchNative typed completion |
| Worker bridge | existing WorkerSession composition, owner-specific CacheSession and WorkerFetchSession |
| Lifetime/GC | native finalizers for handles/DTO, traced object slots for JS edges, original source retirement |
| Interface parity | same native installer and executable through Python and C ABI |
| Evidence | artifacts/native-four/cache/{syntax.json,domain-session.json,acceptance.py}; runtime after.json follows coordinated build |

The isolated 34-check session test proves shared namespace, origin partition,
transaction failure atomicity, deletion/reopen independence and stale handle
rejection. It is not a substitute for native API donor and GC acceptance. All new
Cache adapter files are under 200 lines and split by one responsibility. H1/H2
prerequisite reviews remain in their frozen candidate directories.

## AsyncSource accessor (2026-10-22)

`resource_network::Realm::AsyncSource()` returns the source `BindAsyncHttp` opened for this Realm's Model (null
before binding). Page Load starts a frame's dynamic script fetch on it, so the request runs in the frame's request
context and is dropped when this Realm detaches the source. No new state: the source was already owned here.

## fetch, XHR and sendBeacon on the asynchronous transport (2026-10-22, owner record)

Firefox never blocks the JS thread on a network request; ruyijs ran page `fetch()` (`FetchAction` -> `PerformHttp`),
asynchronous XHR (its delivery task) and `sendBeacon` synchronously on the JS thread. Measured on cloud.tencent.cn
page load: 61 synchronous requests, 10.9 s of JS-thread time.
- `fetch()` (non-blob URLs) now goes through the existing `FetchNative` path (the Cache owner's native fetch:
  `PrepareNativeFetch`, the realm's `asyncSource`, abort handling, `ResponseObject`); the promise settles in the
  response task (`SettleFetch`). blob: URLs keep the synchronous capture at initiation.
- Asynchronous XHR starts its request on `asyncSource` when sent; the delivery task (holding the XHR, kept rooted
  by `PersistentRootedObject` until the response arrives) is posted with the response stored as `prepared`; a
  request aborted or reopened meanwhile is dropped (generation check). Synchronous XHR and Worker XHR
  (`HasRemoteHttp`) are unchanged.
- `sendBeacon` delivery starts the POST on `asyncSource` and returns; the pending budget is released with the
  response (the request record is the completion's target).
- Result: cloud.tencent.cn load: synchronous JS-thread requests 61 -> 9 (stylesheets, the document); examples
  regression unchanged.

## Storage keys and values are DOMStrings (2026-10-22, owner record)

- Bounded context: Resource Network Storage (`resource_network/page_storage.cpp`, the sole Storage installer; store
  `model_storage.cpp` unchanged). `getItem`/`setItem`/`removeItem`/`key` converted keys and values as ByteStrings, so
  any character above U+00FF threw "Storage.setItem requires a key and value". They are now DOMStrings held as sized
  UTF-8 (the encoding host/profile imports already use); U+0000 is kept, a lone surrogate becomes U+FFFD.
- Observation: 网易易盾 regression — qiyukf `localStorage.setItem("DA-LANDPAGE-TITLE", document.title)` with a
  Chinese title threw inside the iframe load handler.
- Tests: `artifacts/storage-unicode/probe.js` (CJK, accents, astral, NUL, non-string values, CJK keys, arity, both
  areas) equals Firefox 158. Not modelled: `key(i)` order (Firefox hash-table order; ruyijs insertion order).

## EventSource (2026-10-23, owner record)

- Bounded context: Resource Network. Domain `domain/resource_network/event_stream.h` (text/event-stream parser: line
  endings, comments, event/data/id/retry fields); adapter `resource_network/event_source.cpp` (connection over
  `FetchNative`, reconnection through the existing `abortDelay` task port) and `event_source_install.cpp` (the sole
  installer: constructor, `close`, `url`, `withCredentials`, `readyState`, `onopen`/`onmessage`/`onerror`, constants).
- State source: instance reserved slots (url, credentials, readyState, handlers, last event ID, reconnection time,
  generation that cancels a pending reconnect after `close()`). No new aggregate state.
- Object factory/installer: `InstallEventSource` called from `InstallPage` (Window) and
  `InstallWorkerTransports` (dedicated, shared and service workers, as Firefox exposes it there). Events are fired by
  Interaction (`Realm::FireNamedMessage`: trusted MessageEvent with the stream's event type, origin, lastEventId).
- Interface parity: C API and Python share the same runtime assembly; nothing interface-specific.
- Tests: `artifacts/event-source/run.py` (shape, descriptors, invalid receiver, call-without-new, invalid URL
  SyntaxError, events/ids/CR-LF/UTF-8/unterminated tail, wrong MIME and 404 fail without reconnect, server close
  re-enters CONNECTING) equals Firefox 158 in Window and Worker, 10/10. Deviation: D139.

## Body bytes() and the Blob byte stream (2026-10-23, owner record)

- Bounded context: Resource Network Body (`body.cpp` installs `bytes` next to `arrayBuffer` on Request and Response,
  Window and Worker; `body_result.cpp` `BodyBytesValue`; `body_stream_consume.cpp` `BodyResultKind::Bytes` for stream
  bodies). `byte_stream_source.cpp` `Realm::NewByteStream` lends the existing body-holder byte stream to Blob.stream().
- State source: the existing BodyRecord; no new state. Installer: unchanged single `InstallBody`.
- Tests: `artifacts/body-bytes/probe.js` (descriptor, order, buffered, empty, stream body, used body, receiver,
  Request, UTF-8) equals Firefox 158, 9/9; Blob stream in `artifacts/blob-stream/probe.js` (WEB_PLATFORM).
