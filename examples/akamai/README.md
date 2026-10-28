# Akamai Bot Manager 3.x — ruyijs 闭环

用 ruyijs 跑通 Akamai BM 3.x 的 sensor_data + sbsd 两道门，拿到真页面。

## 背景

Akamai Bot Manager 3.x 在丽笙酒店（radissonhotels.com）上部署了两道独立的门：

```
403 拒绝 → sensor_data(_abck→~0~) → 200 sec-if-cpt 插页 → sbsd(bm_sc→2/4) → 真页面
```

传统逆向路线需要四层工作：
1. **AST 反混淆**（894KB 混淆 JS → 21 个变换 + 6 张密钥表的运行时字符串解密）
2. **字节码 VM 反编译**（`mst.dvc` 的内嵌 42 条 opcode VM）
3. **加密链纯算还原**（LCG + 92 字符表流替换 + 冒号洗牌 + 版本头 + BBt 耗时头）
4. **sbsd 补环境**（503KB 宿主壳 + 120 个 signals 指纹注入）

ruyijs 提供了**引擎级真实性**（真 SpiderMonkey Firefox 159 + Necko 网络栈），
Akamai 的 sensor_data 和 sbsd 脚本在里面跑和在真 Firefox 里跑一样——**不需要碰任何一层逆向**。

## 用法

```sh
python akamai.py
python akamai.py --proxy http://127.0.0.1:7890
python akamai.py --direct --output result.json
python akamai.py --direct --timeout 120
```

## 流程

1. `navigate()` 到目标 → Necko 发真 Firefox TLS 请求
2. 拿到 2732 字节 sec-if-cpt 挑战页（`_abck=-1`, `bm_sc=1`）
3. 挑战页的 `<script>` 标签自动加载 sensor_data 和 sbsd 脚本
4. sensor_data 脚本在真 SpiderMonkey 中执行，自动采集环境指纹、POST 上报 → `_abck` 更新
5. sbsd 脚本在真 SpiderMonkey 中执行，自动生成挑战响应、POST 上报 → `bm_sc=4`（passed）
6. 挑战页内联 JS 检测到 sbsd 响应，自动 `location.reload(true)`
7. 重新 GET 首页 → 拿到 543K 真页面 ★

**整个流程不注入、不替换任何浏览器 API，不碰 Akamai 源码。**

## 实测结果

| 指标 | 第 1 次 | 第 2 次 |
|---|---|---|
| 耗时 | 26.6s | 27.2s |
| 初始页面 | 2732 字节 (challenge) | 2732 字节 (challenge) |
| `bm_sc` | 1 → **4** (passed) | 1 → **4** (passed) |
| sensor POSTs | 2 × 201 | 2 × 201 |
| sbsd POST | 1 × 200 | 1 × 200 |
| 真页面 | **543,447 字节** | **543,448 字节** |
| 脚本错误 | **0** | **0** |

网络请求序列：

```
GET  /zh-cn/              → 200 (2732 字节 challenge 页)
GET  sbsd 脚本             → 200
GET  sensor_data 脚本      → 200
POST sensor_data #1       → 201
POST sensor_data #2       → 201
POST sbsd (带 ?t=chlgeId) → 200  ← bm_sc 翻 4
GET  /zh-cn/ (reload)     → 200 (543K 真页面 ★)
```

## 为什么能跑通

| 问题 | ruyijs 怎么解决 |
|---|---|
| sensor_data 64 字段指纹采集 | 真 SpiderMonkey + 38 个浏览器上下文，API 全部原生 |
| 加密链（LCG 洗牌 + 92 字符表流替换） | 不碰源码，脚本自己算自己加密 |
| dvc 字节码 VM | 引擎原生执行，字节码在真 SpiderMonkey 里跑 |
| sbsd 120 个 signals | `configure_fingerprint` 配置完整 profile，脚本自己读 |
| 鼠标轨迹 `isTrusted` 检测 | `dispatch_input()` 产生真 trusted 事件（本例无需手动触发） |
| TLS 指纹 | Necko 原生 HTTP/1.1·2·3，与真 Firefox 一模一样 |
| Cookie 管理（_abck / bm_sc / bm_sz） | Necko 自动处理 Set-Cookie |
| 版本多态（key1/ver/din 键序每 10 分钟换） | 不碰源码 = 不受版本影响 |
| hash 自完整性校验 | 不改源码、不插桩 |
| `BBt` 耗时头 | 脚本自己量自己填 |

## 对比

| 维度 | 纯算方案 (L1) | 补环境方案 (L2) | ruyijs 方案 |
|---|---|---|---|
| 工作量 | AST 反混淆 + VM 反编译 + 算法重写 | 503KB 宿主壳 + signals 注入 | **< 300 行 Python** |
| 版本兼容 | 每版需现场提取 key1/ver/din | signals 键前缀每版换 | **不受影响** |
| 鼠标事件 | 构造 60Hz 轨迹模型 | 宿主壳回放轨迹 | **真 trusted 事件** |
| TLS | curl_cffi 模拟 | curl_cffi 模拟 | **Necko 原生** |
| 指纹真实性 | Python 拼装 | Node.js + 补丁 | **真引擎 API** |
