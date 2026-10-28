# W1 消息与传输状态契约

2026-10-03。承接 WORKER_PROTOCOL.md 的已验收 W0 字节编码。此阶段只实现与验证消息 schema、名额登记和信用状态机；实际双管道、协议执行和C++ supervisor另验收。纯状态机不安装浏览器API，不分配domain request ID或维护日志、cookie、页面Promise。

## 共享类型与消息方向

每条消息沿用W0封闭envelope；`epoch`由连接owner比对，`channelSeq`由唯一writer发送时分配且reader验证连续。所有字段必填，除表中明确允许null外不允许缺席或null；未知字段拒绝。

`u64`表示规范十进制字符串。身份epoch/reservationId/requestId非零；seq为0..UINT64_MAX-1；水位可为null表示尚无块。`key`只有requestId:u64、generation:u32(1..UINT32_MAX)、hop:u32(0..19)。partition为0..3的整数，无隐式adapter创建。

headers是最多256个object的数组，每项只有name(ASCII HTTP token)、valueBase64(canonical RFC4648)。值解码为Latin1 bytes，字段累计name字节+value字节+4≤65536；拒绝NUL/CR/LF、除HTAB外的C0和DEL。不得合并或重排此DTO。头的规范组合/guard仍归Resource或传输原始wire处理。

`reason`枚举为cancelled、deadline、admission、closing、failure。`error`为null或object，字段只有kind、phase、code、message、attempt；kind枚举Cancelled/DeadlineExceeded/DnsError/ConnectError/TlsError/ProtocolError/BodyLimit/DecodeError/WorkerExited/AdmissionError，phase为1..64个ASCII `[a-z0-9-]`，code为u32，message为UTF8≤512字节，attempt为0..3。

TLS快照为null或object，只有backend、alpn、version；backend为nss，alpn为http/1.1、h2或h3，version为1..32个可见ASCII字节0x21..0x7e。此字段仅传证据，不更改证书验证或信任配置。HTTP version枚举http/1.1、h2、h3，status整数100..599。

| type | 方向 | payload封闭字段 |
| --- | --- | --- |
| hello | 双向，各自第一帧 | role:"parent"或"worker", pid:非零u32, nonce:32个小写hex且非全零, manifest:64个小写hex, qpcFrequency:非零u64 |
| ready | worker→parent | manifest:64小写hex, abi:1, nss:"3.131", nspr:"4.40", wireProfile:"firefox159" |
| reserve | parent→worker | reservationId:u64, partition, deadlineQpc:非零u64, qpcFrequency:非零u64, method:1..32 ASCII token, url:UTF8非空≤16384字节且无C0/DEL, headers, uploadLength:u32≤16MiB, replayable:true, responseLimit:u32≤16MiB, transportOptions |
| accepted | worker→parent | reservationId:u64, state:"reserved" |
| activate | parent→worker | reservationId:u64, key |
| activated | worker→parent | reservationId:u64, key, state:"queued"或"active" |
| release-reservation | parent→worker | reservationId:u64, reason |
| reservationReleased | worker→parent | reservationId:u64, reason |
| ack-reservation | parent→worker | reservationId:u64 |
| upload-credit | worker→parent | key, grantThrough:水位, ackedThrough:水位, stop:bool |
| download-credit | parent→worker | key, grantThrough:水位, ackedThrough:水位, stop:bool |
| upload-end | parent→worker | key, lastSeq:水位, totalBytes:u32≤16MiB |
| headers | worker→parent | key, status, version, orderedHeaders:headers, tls, attempt:1..3, bodyEncoding:"decoded", headerRepresentation:"encoded" |
| body-end | worker→parent | key, lastSeq:水位, encodedBytes:u32≤16MiB, decodedBytes:u32≤16MiB |
| cancel | parent→worker | key, reason, message:UTF8≤512字节, lastUploadSeq:水位 |
| terminal | worker→parent | key, outcome:"complete"或"error"或"cancelled", error, lastDownloadSeq:水位 |
| released | worker→parent | key, leaseClosed:true, interrupts:0, localResourcesReleased:true, lastUploadAck:水位, lastDownloadSeq:水位 |
| ack-terminal | parent→worker | key, disposition:"accepted"或"discarded" |
| close | parent→worker | reason |
| closed | worker→parent | tickets:0, active:0, queued:0, connections:0, resolverActive:0, credits:0, spools:0 |
| fatal | 双向 | error:非null错误object |

transportOptions仅trustProfile:u32、proxyProfile:u32或null、connectMs/readMs:u32(1..60000)。profile由supervisor注册，schema检查类型/数值但不查询路径或联网。hello的role须匹配发送方。terminal.complete要求error=null；error/cancelled要求非null，其中cancelled只接受Cancelled或DeadlineExceeded。跨字段字节量、实际HTTP协议等在request owner接纳时与已观察事实核对。

## 预约与执行名额

worker registry为唯一transport admission owner。总ticket≤80；reserved+queued≤64；active≤16；每partition最多存在1个adapter，共≤4，adapter寿命和实际connection16另由W2注册器管理。registry仅保存reservationId/key/partition/阶段/绝对期限/terminal-released-acked标记；HTTP metadata通过独立有界所有者持有，不在两处复制。

reserve前parent RetainedBudget先预约已知uploadLength全量及min(responseLimit,4×64KiB)响应初始store空间；worker accepted只保留ticket/metadata/deadline，不建立spool、DNS或连接。reservationId在epoch内由parent单调生成且不复用，worker保存有界ticket及最大见过ID，拒绝重复reserve和回绕。任何schema/配额失败不发布accepted、不更改先前ticket。首版admission拒绝由reservationReleased(reason=admission)表示，但也需要一个预留拒绝回复槽；满80时拒绝新reserve的回复仅用单个epoch lane，发出前暂停读取后续reserve，不能创建第81个ticket/tombstone。

accepted后engine调用原Resource.open_request同步原子提交；成功后activate绑定真实key，失败release-reservation。activate原子绑定且只执行一次；相同ticket重复activate同key只返回现状，不重复联网，换key或将已绑定key用于别的ticket为协议错误。reserved已经到期时返回既有reservationReleased；若此时domain已提交，保留真实失败请求日志，禁止回滚成“从未接纳”。worker不消费原domain active64名额，两者独立准入。

activate先转queued，dispatcher显式claim才转active。claim按等待次序在可运行候选中选择；每partition/origin公平性由W2明确origin键后补，W1只证明有界FIFO及16名额。reserved/queued取消进入终态，不占active；active收到取消只是标记cancel_requested并通知原RequestControl，不提前归还名额。reserved过期/release变reservationReleased tombstone，ack-reservation后删除；已active ticket走以下独立生命周期。

active完成/错误先记录terminal，清理成功再记录released；terminal只发布一次。released要求调用方已经确认exchange/decoder/spool及lease释放，才归还active名额；registry不凭BodyReader.closed自动推断。协议/清理失败由supervisor回收epoch，失效前保留相应ticket和账目。ack-terminal只能在released、上游接纳/丢弃完成、两方向信用归零之后删除ticket。不能因用户已看到取消就复用名额或删除迟到帧登记。

未激活reservation释放没有domain key，绝不发送带伪造requestId的terminal/body。已绑定key只产生请求终态；release-reservation在activated后到达须明确拒绝，parent改用cancel。context关闭停止reserve、取消/释放已有ticket并等待实清理；Job终止并确认退出后才允许集中清空epoch。新epoch禁止自动重放旧请求。

W1纯registry对admission-full只返回显式拒绝结果；epoch回复lane、读取暂停和真实通道故障留I/O阶段验证，不把纯状态机当作完整背压机制。I/O owner在单个rejection lane保留被拒reservationId，接收对应ack-reservation后清空，再接下一个reserve；该ack不送入无此ticket的registry，也不将拒绝消息作为第81个ticket。连接在等待ack时仍必须读取credit/cancel/close等控制消息；第二个reserve明确触发协议错误或有界关闭，不额外保存拒绝队列。所有可能失败的查找、类型与配额检查在状态变更之前进行，公开快照返回独立数据。

已activate但尚未claim的queued取消走key的terminal→released→ack-terminal，released不减active；只有未activate的reserved走reservationReleased。后续response追加store额度失败直接BodyLimit并进入取消/清理，不等待其它请求释放内存，避免多个upload spool占满保留界而形成环等。

合法但因admission/closing/deadline拒绝的reserve推进max_seen，以保证本epoch内同ID不重试；schema/类型错误保持max_seen不变。收到时已过期的reserve直接返回deadline拒绝，使用同一epoch拒绝回复lane，不建立ticket。queued取消标记cancel_requested并记录terminal，返还waiting名额但ticket保留到显式released/credit drain/ack。active取消只置flag，active名额直到released才归还。

registry的key唯一性检查覆盖当前保留的至多80个ticket；ack后不增加无界key历史。真实requestId/generation/hop的长期唯一性由Resource owner提供，parent不得向新reservation重新绑定已退役key。epoch隔离、reservationId单调与已保留key核对分别承担各自边界。

## 每方向四块信用

同一key、同一方向的发送端和接收端各维护自己的wire ledger镜像，无跨进程共享对象。seq从0起；每块1..65536字节；sender只能为已grant的seq预留，再读取上传/decoder、排入writer并标记sent。每方向reserved+填充+queued+sent未ack至多4块，固定每块64KiB计费。为简化可验证顺序，W1每key每方向只允许一个尚未publish的reservation；高层writer可同时保留此前已publish的块。

receiver先预约buffer/store再增加grantThrough；初始可为null（零credit），通常初始3表示seq0..3。grantThrough与ackedThrough累计不减；ackedThrough≤实际已收且已消费的最高连续seq，sender只接受≤实际已publish的ack。grantThrough - ackedThrough≤4（null按数学-1计算），支持合并后跨多块ack。ack重复是幂等，越界/倒序/跳过未消费块为协议错误；错误保持ledger原状态。

receiver核对frame seq恰为下一个已授权序号，在读取payload前声明receive reservation；读取/存入已预约store或明确丢弃后才能ack。发送端收到ack才归还对应slot。低层state API必须区分reserve/publish与receive/consume，不能用“enqueue成功”当作对端消费。
实际sender归还还要求本地writer已完成访问。远端ack可先于本地write返回，通过dispatcher固定绑定分别记录localDone和remoteAck；纯ledger的occupied归零不证明实际lease已经可归还，详见WORKER_BODY_WRITE。

EOF/cancel停止producer分配新seq，只有没有未publish reservation时才seal，lastSeq等于最后已publish的seq或null。read_chunk得到EOF/异常时可abort尚未publish的单一reservation，原seq不发布也不跳号，然后seal。已publish帧继续保持credit，跨通道terminal/EOF先到时receiver等收齐lastSeq，再consume/ack；最后ack使两端归还尚未使用的grant窗口。receiver的stop:true请求producer尽快seal，stop不等于对方已停止或所有数据已释放。

upload-end/cancel携带上传seal，body-end/terminal携带下载seal；同方向重复seal必须值相同。收到小于已接收/已发送或超出grant的seal为错误。取消/过期key的迟到已授权块仍读取有限payload并消费/丢弃，归还信用；未知key/旧epoch等没有reservation的头在分配payload前触发对应连接失败。registry等credit归零才ack-terminal；资源释放与wire slot释放是两个独立事实。

sender seal之后可立即撤销本地未使用grant区间，但已publish未ack的slot继续持有；receiver仅在见到seal、收齐并consume至lastSeq后才标记drained，空流同样需要显式seal。sender只有sealed且全部publish均已ack才drained；stop:true只是停止请求，不单独证明drained。最终累计ack可与原值相同，不靠是否有新ack数值来识别空流完成；terminal/released与同方向seal分别有独立事实。

W1纯ledger不分配真实64KiB buffer或BodyStore，不把计数当作已经受控的进程内存。调用方必须以transactional hooks先取得实际内存/存储reservation，再发布grant；该真实接线、zero-credit取消和两通道阻塞另在worker I/O套件验证。

## 文件与验收

`transport/necko/worker_schema*.py`唯一处理消息payload白名单/范围/方向；与W0解码后调用。`worker_registry.py`及内部模型独占准入阶段，`worker_credit.py`等独占单方向wire ledger。各手写≤200行，多文件按schema类型/准入/信用分拆。它们不导入JSAPI、domain或requests.Session。后续C++端复用同一fixtures实现payload接纳和parent ledger，避免两端宽松程度不同。

测试必须覆盖全部type/方向、u32 bool与u64 string区别、headers64KiB/256/Latin1/canonical base64，80/64/16边界、失败原子性、reserve无执行、activate只一次、expire/activate竞态、terminal≠released、released≠acked、重复/越界/迟到identity、4块耗尽/累计ack/abort reservation/seal空流/跨通道先后/错误后快照保持。模型仅作协议状态对照，真实worker、Job与三协议测试继续作为单独门槛。
