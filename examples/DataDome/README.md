# DataDome interstitial device-check

纯 ruyijs + necko 闭环跑通 DataDome interstitial 全流程——从 403 到首页 200。

不注入、不替换、不 hook 任何 DataDome 代码——DataDome 自己的 JS 在 ruyijs 里原样跑，
自己采集指纹、自己算 plv2、自己跑 VM 算 plv3、自己 POST 提交，拿到 datadome cookie
后由 necko 原生 cookie jar 携带，再请求首页拿到 200 + 完整 HTML。

```
python datadome.py --direct
python datadome.py --proxy http://127.0.0.1:7890
python datadome.py --output result.json
```

## 它做了什么

```text
首页 GET https://www.mouser.cn/
  ↓ 403（DataDome 拦截，注入 c.js）
  ↓ c.js 创建 interstitial iframe → geo.captcha-delivery.com/interstitial/
  ↓ iframe 加载 interstitial.1.35.x.js（~640 KB webpack bundle）

interstitial.js 内部：
  ↓ webpack 模块 516 采集 228+ 个环境字段
      navigator / screen / WebGL / Canvas / AudioContext / CSS / fonts / ...
  ↓ 模块 516 编码 → plv2
      djb2 哈希 + xorshift32 + 双 XOR 流加密 + 自定义 base64
  ↓ webpack 模块 872 启动内层字节码 VM
      102 opcode / 57227 条指令 / 146 个函数入口
  ↓ VM 采集 + 仿射变换 + 三张查表 XOR → plv3
      base64url 编码的 ~380 字节密文

  ↓ POST https://geo.captcha-delivery.com/interstitial/
      payload = plv2  (~7100 chars)
      plv3             (~500 chars)
      cid / hash / referer / url / s / e / env / userEnv / seed / b / dm / ps

  ↓ 服务端返回 datadome cookie → necko cookie jar 存储

携 cookie fetch 首页
  ↓ GET https://www.mouser.cn/  (Cookie: datadome=...)
  ↓ 200 + 完整 HTML（~149 KB）
```

## 为什么是纯闭环

| 组件 | 谁提供的 | 作用 |
|------|----------|------|
| SpiderMonkey (Firefox 159) | ruyijs 内置 | 执行 DataDome 全部 JS，包括 VM 字节码的 `new Function` 编译 |
| Necko 网络栈 | ruyijs 内置 | 所有 HTTP/HTTPS 请求 + cookie jar 管理 |
| 38 个浏览器 Context | ruyijs 实现 | window / document / navigator / crypto / WebGL / Canvas / PerformanceTiming 等 |
| DataDome 代码 | 线上原样加载 | 采集、编码、VM、提交——一个字节都没改 |

没有 Playwright / Puppeteer / Selenium，没有真实浏览器窗口，没有 GPU 渲染器。

## 输出示例

```
[*] proxy=direct
{"stage": "navigate",     "t": 3.94}
{"stage": "interstitial", "t": 8.46, "bodyClass": "dd-response-page--device-check", "ddm": true}
{"stage": "ddm",          "t": 8.46, "cid": "wxS~5mhRf~5lcloli~n6...", "hash": "10D43DA6B79A5089E1A7846864D6BD"}
{"stage": "post",         "t": 10.3, "payload_length": 7184, "plv3_length": 498}
{"stage": "cookie",       "t": 10.3, "datadome": "wxS~5mhRf~5lcloli~n6t6U~4AYOMRQ..."}
{"stage": "homepage",     "t": 13.15, "status": 200, "length": 149143, "title": "... Mouser Electronics"}

============================================================
[OK] DataDome interstitial device-check
  plv2:      7184 chars
  plv3:      498 chars
  Homepage:  200  149143 bytes  ... Mouser Electronics
  Time:      13.2s
```

退出码 0 = 首页 200 拿到。

## plv2 算法概要

> `plv2 = 自定义base64( XOR递减计数器( 流2_XOR( 流1_XOR("{" + 字段JSON) ) + "}" ) )`

- **djb2 变体哈希** `C(s)`: `B = (B << 5) - B + charCodeAt(i) | 0`，零映射到 `1789537805`
- **xorshift32** `D(x)`: `x ^= x<<13; x ^= x>>17; x ^ (x<<5)`
- **流 1 种子**: `1370015378 ^ djb2(ddm.hash) ^ (-883841716)`（注意 `9959949970` 的 ToInt32 是 `1370015378`）
- **流 2 种子**: `1809053797 ^ djb2(ddm.cid)`
- **计数器初值**: `(Date.now() >> 3) ^ (-1857011797)`（`11027890091` 的 ToInt32）
- **自定义 base64 字母表**: 64 字符，与标准 base64 不同

外部输入只有三个：`ddm.hash`、`ddm.cid`、`Date.now()`。

## plv3 算法概要

> `plv3 = base64url( ~380 字节密文 )`
> `密文[n] = f_点(明文[i]) ^ 表148[j2] ^ 表40[n & 255]`

- 内层字节码 VM：218109 字节字节码，102 个 handler（槽号即 opcode），`new Function` 编译
- 单个 `MEM` 数组 229021 槽；静态区基址 222233
- ~60 个加密点，每个字段独立的仿射变换 + 两次查表 XOR
- 明文是「字段值 + 8 字节哈希」的混合二进制，不是文本
- 唯一出口 `op65` 在 pc=218108：`RESULT.r = MEM[3394]`

## 参考文档

| 文档 | 内容 |
|------|------|
| `DataDome plv2(payload).md` | plv2 算法完整闭合（djb2、xorshift32、三层 XOR、自定义 base64） |
| `DataDome plv3(内层VM).md` | plv3 字节码 VM 逆向（102 opcode、57227 条指令、仿射+查表加密） |
| `plv2 字段全表(228).md` | 228 个采集字段的名称、值、来源 |
| `DataDome 环境检测清单.md` | ruyijs 需实现的浏览器 API 清单 |

---

## 全量代码

```python
"""DataDome interstitial device-check：ruyijs 跑通 plv2 + plv3 全流程并拿到首页。

python datadome.py
python datadome.py --proxy http://127.0.0.1:7890 --output result.json
python datadome.py --direct

流程：
  首页 403 → DataDome 注入 interstitial iframe（device-check 模式）
  → interstitial.js 采集 228+ 个环境字段（文档 plv2 字段全表）
  → 模块 516 生成 plv2（djb2 + xorshift32 + 双 XOR 流 + 自定义 base64）
  → 模块 872 内层 VM 生成 plv3（字节码 VM + 仿射变换 + 查表 XOR + base64url）
  → POST /interstitial/ 提交 payload(plv2) + plv3
  → 服务端返回 datadome cookie
  → 携 cookie 重新请求首页 → 200 + 完整 HTML

退出码 0 表示首页 200 拿到。

参考文档：
  DataDome plv2(payload).md      — plv2 算法完整闭合
  DataDome plv3(内层VM).md       — plv3 字节码 VM 逆向
  plv2 字段全表(228).md          — 228 个采集字段溯源
  DataDome 环境检测清单.md       — ruyijs 需要实现的浏览器 API 清单
"""
import argparse
import json
import os
from pathlib import Path
import re
import socket
import sys
from time import monotonic
from urllib.parse import parse_qs

ROOT = Path(__file__).resolve().parents[2]
sys.dont_write_bytecode = True
if (ROOT / "dist" / "js.exe").is_file() or (ROOT / "dist" / "js").is_file() or os.environ.get("RUYIJS_SHELL"):
    sys.path.insert(0, str(ROOT / "python"))
from ruyijs import Context, JSError  # noqa: E402

HOME = "https://www.mouser.cn/"
FRAME = "document.querySelector('iframe[src*=\"captcha-delivery\"]')"
ENVIRONMENT = {"screen": {"width": 1920, "height": 1080, "availWidth": 1920, "availHeight": 1040},
               "viewport": {"width": 1904, "height": 987}, "locale": {"timezone": "Asia/Shanghai"}}

READ_INTERSTITIAL = FRAME + """&&(()=>{
  const f=""" + FRAME + """,d=f.contentDocument,w=f.contentWindow;
  return {bodyClass:d.body&&d.body.className, title:d.title,
    ddm:!!w.ddm, interstitialCallback:typeof w.interstitialCallback,
    cid:w.ddm&&w.ddm.cid&&w.ddm.cid.slice(0,20)+'...'}})()"""


def auto_proxy():
    for name in ("HTTPS_PROXY", "https_proxy", "HTTP_PROXY", "http_proxy"):
        if os.environ.get(name):
            return os.environ[name]
    try:
        with socket.create_connection(("127.0.0.1", 7890), timeout=0.3):
            return "http://127.0.0.1:7890"
    except OSError:
        return None


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    proxy_group = parser.add_mutually_exclusive_group()
    proxy_group.add_argument("--proxy", help="HTTP(S) 代理；默认自动检测")
    proxy_group.add_argument("--direct", action="store_true", help="不使用代理直连")
    parser.add_argument("--output", type=Path, default=Path(__file__).with_name("datadome_result.json"),
                        help="JSON 结果输出路径")
    args = parser.parse_args()
    selected = None if args.direct else (args.proxy or auto_proxy())

    report = {"status": "error", "proxy": selected or "direct", "steps": [], "scriptErrors": [],
              "payload": None, "plv3": None, "homepage": None}
    started = monotonic()

    def ts():
        return round(monotonic() - started, 2)

    def log(stage, **kw):
        entry = {"stage": stage, "t": ts(), **kw}
        report["steps"].append(json.loads(json.dumps(entry, default=str)))
        line = json.dumps(entry, ensure_ascii=False, default=str)
        print(line[:600], flush=True)

    print(f"[*] proxy={selected or 'direct'}", flush=True)

    with Context(timeout=90, proxy=selected, environment=ENVIRONMENT) as context:
        def run(ms=50, tasks=200):
            try:
                context.advance(ms, max_tasks=tasks)
            except JSError as error:
                if len(report["scriptErrors"]) < 20:
                    report["scriptErrors"].append(f"{error.name}: {error.message[:200]}")

        def ev(source):
            try:
                return context.eval(source)
            except JSError as error:
                return "JSERR " + str(error)[:200]

        # ── Step 1: navigate → DataDome 403 ──
        context.navigate(HOME)
        log("navigate", url=HOME)

        # ── Step 2: wait for interstitial iframe ──
        iframe_info = None
        for _ in range(200):
            run(200)
            state = ev(READ_INTERSTITIAL)
            if isinstance(state, dict) and state.get("ddm"):
                iframe_info = state
                break
        if not iframe_info:
            log("no_iframe")
            report["error"] = "DataDome interstitial iframe 未加载"
            raise SystemExit(1)
        log("interstitial", **iframe_info)

        # Read ddm (the three external inputs: hash, cid, Date.now)
        ddm = ev(f"""(()=>{{const d={FRAME}.contentWindow.ddm;
          return {{cid:d.cid,hash:d.hash,s:d.s,referer:d.referer}}}})()""")
        log("ddm", **(ddm if isinstance(ddm, dict) else {"raw": ddm}))

        # ── Step 3: wait for interstitial POST (plv2 + plv3) ──
        post_found = None
        for _ in range(300):
            run(300)
            posts = [r for r in context.net_log()
                     if r.get("method") == "POST" and "/interstitial/" in str(r.get("url", ""))
                     and "captcha-delivery" in str(r.get("url", ""))]
            if posts:
                post_found = posts[-1]
                break

        if not post_found:
            log("no_post", error="interstitial POST 未发出")
            report["error"] = "interstitial POST 未发出"
            raise SystemExit(1)

        body = str(post_found.get("requestBody", ""))
        params = parse_qs(body)
        payload = params.get("payload", [""])[0]
        plv3 = params.get("plv3", [""])[0]
        report["payload"] = payload
        report["plv3"] = plv3

        log("post", status=post_found.get("status"),
            payload_length=len(payload), plv3_length=len(plv3),
            payload_head=payload[:60] + "..." if len(payload) > 60 else payload,
            plv3_head=plv3[:60] + "..." if len(plv3) > 60 else plv3,
            other_params={k: v[0][:50] for k, v in params.items() if k not in ("payload", "plv3")})

        # ── Step 4: fetch homepage with datadome cookie ──
        cookie = str(ev("document.cookie") or "")
        dd_cookie = ""
        if "datadome=" in cookie:
            dd_cookie = cookie.split("datadome=")[-1].split(";")[0]
        log("cookie", datadome=dd_cookie[:40] + "..." if len(dd_cookie) > 40 else dd_cookie)

        # necko cookie jar already holds the datadome cookie — fetch with credentials
        ev(r"""
        fetch('https://www.mouser.cn/', {
            credentials: 'include',
            headers: {
                'Accept': 'text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8',
                'Accept-Language': 'zh-CN,zh;q=0.9,en-US;q=0.8,en;q=0.7'
            }
        }).then(async r => {
            const text = await r.text();
            const m = text.match(/<title>([^<]*)<\/title>/);
            globalThis.__home = {
                status: r.status,
                length: text.length,
                title: m ? m[1].trim() : null,
                mouser: text.indexOf('Mouser Electronics') >= 0,
                head: text.slice(0, 500)
            };
        }, e => {
            globalThis.__home = {error: String(e)};
        });
        undefined
        """)

        home = None
        for _ in range(200):
            run(200)
            home = ev("globalThis.__home")
            if isinstance(home, dict):
                break

        if isinstance(home, dict) and home.get("status") == 200 and home.get("mouser"):
            report["status"] = "passed"
            report["homepage"] = {
                "status": home["status"],
                "length": home["length"],
                "title": home.get("title"),
            }
            log("homepage", status=200, length=home["length"], title=home.get("title"),
                head=home.get("head", "")[:200])
        elif isinstance(home, dict) and home.get("status") == 403:
            report["status"] = "submitted"
            report["homepage"] = {"status": 403}
            log("homepage", status=403, msg="datadome cookie 未放行，服务端要求更高验证")
        else:
            report["status"] = "submitted"
            log("homepage", raw=home)

        report["net"] = [{k: v for k, v in r.items() if k != "requestBody"}
                         for r in context.net_log()]
        report["console"] = context.console_log()[-20:]

    report["elapsedSeconds"] = round(monotonic() - started, 3)
    args.output.write_text(json.dumps(report, ensure_ascii=False, indent=1, default=str), "utf-8")

    ok = report["status"] == "passed"
    print(f"\n{'='*60}")
    print(f"[{'OK' if ok else 'PARTIAL'}] DataDome interstitial device-check")
    print(f"  plv2:      {len(report.get('payload') or '')} chars")
    print(f"  plv3:      {len(report.get('plv3') or '')} chars")
    hp = report.get("homepage") or {}
    if hp.get("status") == 200:
        print(f"  Homepage:  200  {hp.get('length')} bytes  {hp.get('title', '')}")
    else:
        print(f"  Homepage:  {hp.get('status', '?')}  (cookie 未通过服务端评分)")
    print(f"  Time:      {report['elapsedSeconds']}s")
    print(f"  Output:    {args.output}")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
```
